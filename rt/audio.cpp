// Audio mixer: MP3 decoded in streaming fashion (dr_mp3) from the SWF data.
#include "display.h"
#include "platform.h"
#include <SDL.h>
#include <functional>
#include <algorithm>
#include <mutex>
#include <string>
#include <cctype>
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/drmp3.h"

namespace fl {
extern std::vector<std::function<void(Tracer&)>> g_rootTracers;
extern float g_masterVolume;
float g_audioVolume = 1.0f;   // global volume from the settings

static const int kRate = 44100;
static int g_rate = kRate;   // output rate: the device's own (Android: usually 48 kHz), 44.1 kHz in tests
struct Chan {
    bool active = false;
    drmp3 dec; bool decOk = false;
    SoundDef* def = nullptr; StreamInfo* stream = nullptr;
    Object* owner = nullptr;
    int loops = 1; uint64_t startFrame = 0; uint64_t skip = 0;
    float vol = 1, pan = 0;
    int srcRate = kRate, srcCh = 1;
    double pos = 0;                 // fractional position in the source buffer
    bool eof = false;               // no more source data: play out what is left in buf
    float buf[2][4096]; int bufLen = 0; int bufBase = 0;   // decoded samples (per channel)
    uint64_t framesConsumed = 0;    // source frames consumed since startFrame (for position)
    int gen = 0;
};
static Chan g_chans[48];
static SDL_AudioDeviceID g_dev = 0;
static int g_genCounter = 0;

static std::recursive_mutex g_devMu;   // g_dev can be reopened by the main thread (output device changes)
void audioLock() { g_devMu.lock(); if (g_dev) SDL_LockAudioDevice(g_dev); }
void audioUnlock() { if (g_dev) SDL_UnlockAudioDevice(g_dev); g_devMu.unlock(); }

static bool chanSeek(Chan& c, uint64_t frame) {
    if (!c.decOk) return false;
    bool ok = drmp3_seek_to_pcm_frame(&c.dec, frame) != 0;
    c.bufLen = 0; c.pos = 0; c.eof = false; c.framesConsumed = frame >= c.startFrame ? frame - c.startFrame : 0;
    return ok;
}
static void chanFree(Chan& c) { if (c.decOk) drmp3_uninit(&c.dec); c.decOk = false; c.active = false; c.owner = nullptr; c.def = nullptr; c.stream = nullptr; }

// refills the source buffer; returns false at the end of the sound (no more loops)
static bool refill(Chan& c) {
    float tmp[4096 * 2];
    int want = 2048;
    drmp3_uint64 got = drmp3_read_pcm_frames_f32(&c.dec, want, tmp);
    if (got == 0) {
        if (c.loops > 1) {
            c.loops--;
            drmp3_seek_to_pcm_frame(&c.dec, c.skip);
            c.framesConsumed = 0;
            got = drmp3_read_pcm_frames_f32(&c.dec, want, tmp);
            if (got == 0) return false;
        } else return false;
    }
    for (drmp3_uint64 i = 0; i < got; i++) {
        float l = tmp[i * c.srcCh], r = c.srcCh > 1 ? tmp[i * c.srcCh + 1] : l;
        c.buf[0][i] = l; c.buf[1][i] = r;
    }
    c.bufLen = (int)got;
    return true;
}

static void mix(void*, Uint8* stream, int len) {
    float* out = (float*)stream; int frames = len / (int)(sizeof(float) * 2);
    std::fill(out, out + frames * 2, 0.f);
    float master = g_masterVolume * g_audioVolume;
    for (auto& c : g_chans) {
        if (!c.active) continue;
        double step = (double)c.srcRate / g_rate;
        float lv = c.vol * master * (c.pan > 0 ? 1 - c.pan : 1), rv = c.vol * master * (c.pan < 0 ? 1 + c.pan : 1);
        for (int i = 0; i < frames; i++) {
            // keep 3 samples of history across refills: the cubic interpolation reads one before and two after
            while (!c.eof && c.pos >= c.bufLen - 2) {
                int k = std::min(3, c.bufLen);
                float hl[3], hr[3];
                for (int q = 0; q < k; q++) { hl[q] = c.buf[0][c.bufLen - k + q]; hr[q] = c.buf[1][c.bufLen - k + q]; }
                int oldLen = c.bufLen;
                c.framesConsumed += oldLen - k;   // (before refill: a loop restart resets it)
                if (!refill(c)) { c.framesConsumed -= oldLen - k; c.eof = true; break; }   // (buf untouched: the tail is still played)
                memmove(&c.buf[0][k], &c.buf[0][0], sizeof(float) * c.bufLen); memmove(&c.buf[1][k], &c.buf[1][0], sizeof(float) * c.bufLen);
                for (int q = 0; q < k; q++) { c.buf[0][q] = hl[q]; c.buf[1][q] = hr[q]; }
                c.bufLen += k;
                c.pos -= oldLen - k;
            }
            if (c.eof && c.pos >= c.bufLen - 1) { c.active = false; break; }
            int ip = (int)c.pos; float t = (float)(c.pos - ip);
            int im = ip > 0 ? ip - 1 : 0, i2 = std::min(ip + 2, c.bufLen - 1);
            // Catmull-Rom (cubic Hermite): noticeably cleaner than linear when 11/22 kHz sounds are upsampled
            auto cr = [t](float pm, float p0, float p1, float p2) {
                return p0 + 0.5f * t * (p1 - pm + t * (2 * pm - 5 * p0 + 4 * p1 - p2 + t * (3 * (p0 - p1) + p2 - pm)));
            };
            float l = cr(c.buf[0][im], c.buf[0][ip], c.buf[0][ip + 1], c.buf[0][i2]);
            float r = cr(c.buf[1][im], c.buf[1][ip], c.buf[1][ip + 1], c.buf[1][i2]);
            out[i * 2] += l * lv; out[i * 2 + 1] += r * rv;
            c.pos += step;
        }
        if (!c.active) { if (c.decOk) drmp3_uninit(&c.dec); c.decOk = false; }
    }
    for (int i = 0; i < frames * 2; i++) out[i] = std::max(-1.f, std::min(1.f, out[i]));
}

// ---- output device
// SDL normally follows the system's default output. On PC, plugging a controller with its own audio (DualSense,
// DualShock 4, Xbox headset jack) can make it the default, and the sound "disappears" into the controller. So the
// device that was the default when the game started is opened by name and kept; when it goes away, or the default
// changes to something that isn't a controller (headphones plugged in...), the game moves to the new default.
static std::string g_devName;   // "" = SDL's default device
static double g_devCheckAt[2] = {-1, -1};
static bool isControllerAudio(const std::string& n) {
    std::string l; for (char ch : n) l += (char)tolower((unsigned char)ch);
    for (const char* k : {"controller", "dualsense", "dualshock", "xbox", "gamepad", "joy-con"}) if (l.find(k) != std::string::npos) return true;
    return false;
}
static std::string defaultOutputName() {
#if SDL_VERSION_ATLEAST(2, 24, 0)
    char* name = nullptr; SDL_AudioSpec spec{};
    if (SDL_GetDefaultAudioInfo(&name, &spec, 0) == 0 && name) { std::string r = name; SDL_free(name); return r; }
#endif
    return std::string();
}
static bool openDevice(const std::string& name) {
    SDL_AudioSpec want{}, have{};
    // mix directly at the device's rate (one resampling step instead of ours + SDL's conversion)
    want.freq = kIsAndroid ? 48000 : kRate; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = kIsAndroid ? 512 : 1024; want.callback = mix;   // (Android: ~11 ms)
    SDL_AudioDeviceID d = SDL_OpenAudioDevice(name.empty() ? nullptr : name.c_str(), 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!d) { logf("audio: %s: %s\n", name.empty() ? "default device" : name.c_str(), SDL_GetError()); return false; }
    if (have.freq >= 8000 && have.freq <= 192000) g_rate = have.freq;
    g_dev = d; g_devName = name;
    logf("audio: %s, %d Hz, %d channels, %d-sample buffer%s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?", have.freq, have.channels, have.samples,
         name.empty() ? "" : " (fixed output device)");
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}
void audioInit(bool device) {
    if (!device) { g_rootTracers.push_back([](Tracer& tr) { for (auto& c : g_chans) if (c.active) tr.mark(c.owner); }); return; }
    std::string def = (kIsAndroid || kIsSwitch) ? std::string() : defaultOutputName();
    if (def.empty() || !openDevice(def)) openDevice(std::string());
    g_rootTracers.push_back([](Tracer& tr) { audioLock(); for (auto& c : g_chans) if (c.active) tr.mark(c.owner); audioUnlock(); });
}
// main thread: an audio device was added or removed (the system's default is checked a little later: it changes
// asynchronously)
void audioDevicesChanged(double now) {
    if (kIsAndroid || kIsSwitch || g_devName.empty() || !g_dev) return;
    g_devCheckAt[0] = now + 0.5; g_devCheckAt[1] = now + 2.5;
}
void audioDeviceTick(double now) {
    for (double& at : g_devCheckAt) {
        if (at < 0 || now < at) continue;
        at = -1;
        std::string def = defaultOutputName();
        bool lost = SDL_GetAudioDeviceStatus(g_dev) == SDL_AUDIO_STOPPED;
        bool moved = !def.empty() && def != g_devName && !isControllerAudio(def);
        if (!lost && !moved) continue;
        std::string want = moved ? def : std::string();
        if (lost && !moved && !def.empty() && !isControllerAudio(def)) want = def;
        logf("audio: output device %s, switching to %s\n", lost ? "lost" : "changed", want.empty() ? "the default" : want.c_str());
        std::lock_guard<std::recursive_mutex> lk(g_devMu);
        SDL_CloseAudioDevice(g_dev); g_dev = 0;
        if (want.empty() || !openDevice(want)) openDevice(std::string());
    }
}

static Chan* allocChan() {
    for (auto& c : g_chans) if (!c.active) { if (c.decOk) { drmp3_uninit(&c.dec); c.decOk = false; } return &c; }
    // steal the oldest channel
    Chan* best = &g_chans[0];
    for (auto& c : g_chans) if (c.gen < best->gen) best = &c;
    chanFree(*best); return best;
}

int soundStart(SoundDef* s, double offsetSec, int loops, Object* owner, float vol, float pan) {
    if (!s || s->format != 2 || !s->size) return -1;
    audioLock();
    Chan* c = allocChan();
    c->decOk = drmp3_init_memory(&c->dec, s->data, s->size, nullptr) != 0;
    if (!c->decOk) { audioUnlock(); return -1; }
    c->def = s; c->stream = nullptr; c->owner = owner; c->loops = std::max(1, loops); c->vol = vol; c->pan = pan;
    c->srcRate = (int)c->dec.sampleRate; c->srcCh = (int)c->dec.channels;
    c->skip = s->seek > 0 ? (uint64_t)s->seek : 0;
    c->startFrame = c->skip + (uint64_t)std::max(0.0, offsetSec * c->srcRate);
    if (c->startFrame) chanSeek(*c, c->startFrame);
    c->bufLen = 0; c->pos = 0; c->eof = false; c->framesConsumed = 0;
    c->active = true; c->gen = ++g_genCounter;
    int idx = (int)(c - g_chans);
    audioUnlock();
    return idx;
}

int streamStart(StreamInfo* st, int frame0, Object* owner) {
    if (!st || st->mp3.empty()) return -1;
    audioLock();
    Chan* c = allocChan();
    c->decOk = drmp3_init_memory(&c->dec, st->mp3.data(), st->mp3.size(), nullptr) != 0;
    if (!c->decOk) { audioUnlock(); return -1; }
    c->def = nullptr; c->stream = st; c->owner = owner; c->loops = 1; c->vol = 1; c->pan = 0;
    c->srcRate = (int)c->dec.sampleRate; c->srcCh = (int)c->dec.channels; c->skip = 0;
    c->startFrame = 0;
    uint64_t target = frame0 < (int)st->frameSample.size() ? st->frameSample[frame0] : 0;
    if (target) chanSeek(*c, target); else { c->bufLen = 0; c->pos = 0; c->eof = false; c->framesConsumed = 0; }
    c->active = true; c->gen = ++g_genCounter;
    int idx = (int)(c - g_chans);
    audioUnlock();
    return idx;
}

void streamSync(int ch, StreamInfo* st, int frame0) {
    if (ch < 0 || ch >= 48) return;
    audioLock();
    Chan& c = g_chans[ch];
    if (c.active && c.decOk && c.stream == st && frame0 < (int)st->frameSample.size()) {
        // actual playback position: the decoder is at currentPCMFrame and the buffer holds the previous bufLen
        // frames (buf[k] = currentPCMFrame - bufLen + k). (A counter used to be used instead, but it drifted:
        // it resynced on almost every tick, and resyncing backwards decodes the MP3 from the start:
        // up to ~20 ms per tick on PC and much more on Switch.)
        double cur = (double)c.dec.currentPCMFrame - c.bufLen + c.pos;
        double want = st->frameSample[frame0];
        if (std::fabs(cur - want) > c.srcRate * 0.2) {
            static bool dbg = getenv("ISAAC_STREAMDBG") != nullptr;
            if (dbg) fprintf(stderr, "[stream] seek frame0=%d cur=%.0f want=%.0f rate=%d buf=%d pos=%.1f\n", frame0, cur, want, c.srcRate, c.bufLen, c.pos);
            chanSeek(c, (uint64_t)want);
        }
    }
    audioUnlock();
}

bool chanAlive(int ch) { if (ch < 0 || ch >= 48) return false; audioLock(); bool a = g_chans[ch].active; audioUnlock(); return a; }
void soundStopChan(int ch) { if (ch < 0 || ch >= 48) return; audioLock(); chanFree(g_chans[ch]); audioUnlock(); }
void soundStopOwner(Object* owner) { audioLock(); for (auto& c : g_chans) if (c.active && c.owner == owner) chanFree(c); audioUnlock(); }
void soundStopAll() { audioLock(); for (auto& c : g_chans) if (c.active) chanFree(c); audioUnlock(); }
void soundStopDef(SoundDef* s) { audioLock(); for (auto& c : g_chans) if (c.active && c.def == s) chanFree(c); audioUnlock(); }
bool soundPlayingDef(SoundDef* s) { audioLock(); bool r = false; for (auto& c : g_chans) if (c.active && c.def == s) r = true; audioUnlock(); return r; }
void soundSetVolumeOwner(Object* owner, float vol, float pan) {
    audioLock(); for (auto& c : g_chans) if (c.active && c.owner == owner) { c.vol = vol; c.pan = pan; } audioUnlock();
}
double soundPositionMs(Object* owner, bool* active) {
    audioLock();
    double r = 0; int best = -1;
    for (auto& c : g_chans) if (c.owner == owner && c.decOk && c.gen > best) {
        best = c.gen;
        r = ((double)c.framesConsumed + c.pos + (double)(c.startFrame > c.skip ? c.startFrame - c.skip : 0)) * 1000.0 / c.srcRate;
    }
    audioUnlock();
    if (active) *active = best >= 0;
    return r;
}

// ---- audio dump to WAV (device-less tests): mixes 1/30 s per tick
static FILE* g_wav = nullptr; static uint32_t g_wavFrames = 0;
void audioDumpTick(const char* path) {
    if (!path) {   // (test mode without --wav: still consume 1/30 s per tick, as in real time)
        static double acc = 0; acc += kRate / 30.0; int n = (int)acc; acc -= n;
        std::vector<float> buf(n * 2);
        audioLock(); mix(nullptr, (Uint8*)buf.data(), n * 2 * sizeof(float)); audioUnlock();
        return;
    }
    if (!g_wav) { g_wav = fopen(path, "wb"); if (!g_wav) return; uint8_t h[44] = {0}; fwrite(h, 1, 44, g_wav); }
    static double accum = 0; accum += kRate / 30.0; int n = (int)accum; accum -= n;
    std::vector<float> buf(n * 2);
    audioLock(); mix(nullptr, (Uint8*)buf.data(), n * 2 * sizeof(float)); audioUnlock();
    std::vector<int16_t> s16(n * 2); for (int i = 0; i < n * 2; i++) s16[i] = (int16_t)(buf[i] * 32767);
    fwrite(s16.data(), 2, n * 2, g_wav); g_wavFrames += n;
}
void audioDumpClose() {
    if (!g_wav) return;
    uint32_t dataBytes = g_wavFrames * 4; uint8_t h[44];
    auto w32 = [&](int o, uint32_t v) { h[o] = v; h[o + 1] = v >> 8; h[o + 2] = v >> 16; h[o + 3] = v >> 24; };
    auto w16 = [&](int o, uint16_t v) { h[o] = v; h[o + 1] = v >> 8; };
    memcpy(h, "RIFF", 4); w32(4, 36 + dataBytes); memcpy(h + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2); w32(24, kRate);
    w32(28, kRate * 4); w16(32, 4); w16(34, 16); memcpy(h + 36, "data", 4); w32(40, dataBytes);
    fseek(g_wav, 0, SEEK_SET); fwrite(h, 1, 44, g_wav); fclose(g_wav); g_wav = nullptr;
}
}  // namespace fl
