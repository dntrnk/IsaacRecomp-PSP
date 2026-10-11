#include "platform.h"
#include "core.h"
#include <cerrno>
#include <cstring>
#include <exception>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <algorithm>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace fl {
static const Limits kBig   = {400000, 192u << 20, 96u << 20, 160u << 20, 16, 48u << 20, 3, 768, 2.0, false};
static const Limits kSmall = {150000, 128u << 20, 32u << 20,  48u << 20,  6, 24u << 20, 2, 160, 3.0, true};
Limits g_lim = kBig;
void platformInitLimits() {
#if defined(__SWITCH__)
    int used, total; platformMemInfo(&used, &total);
    g_lim = total >= 1500 ? kBig : kSmall;
    if (!g_lim.small) g_lim.pboBytes = 32u << 20;   // (application mode: ~3 GB; atlas as on PC: the preload fits)
    g_lim.gcSweepMs = 3.0;
#elif defined(FL_SWITCH_MEMORY)
    g_lim = kSmall;
#else
    g_lim = kBig;
#endif
}

// Layout on Switch (the application has cores 0-2; priority: lower number = more urgent):
//   core 0: render (main thread, 0x2C) and, below it, one render rasterizer and one logic rasterizer
//   core 1: logic (0x2C), alone
//   core 2: render rasterizer (0x2D) and, below it, the logic one (0x3B)
// This way the renderer never waits for the logic to release a core (Horizon does not time-slice between
// threads of the same priority: one does not progress until the other blocks).
int platformPoolWorkers(ThreadRole role) {
#if defined(__SWITCH__)
    return role == ROLE_LOGIC ? 2 : 1;
#else
    (void)role;
    unsigned hc = std::thread::hardware_concurrency();
    return hc > 1 ? (int)std::min(hc - 1, 7u) : 0;
#endif
}
void platformPoolWorkerPlace(ThreadRole role, int i, int* core, int* prio) {
    if (role == ROLE_LOGIC) { *core = i == 0 ? 2 : 0; *prio = i == 0 ? 0x3B : 0x3D; }
    else { *core = 2; *prio = 0x2D; }
}
int platformRasterServiceWorkers() {
#if defined(__SWITCH__)
    return 2;
#else
    unsigned hc = std::thread::hardware_concurrency();
    return hc > 1 ? (int)std::min(hc - 1, 4u) : 1;
#endif
}
void platformRasterServicePlace(int i, int* core, int* prio) { *core = i == 0 ? 2 : 0; *prio = i == 0 ? 0x2D : 0x3C; }
void platformSetThread(int core, int prio) {
#ifdef __SWITCH__
    if (core >= 0) svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
    if (prio >= 0) svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
#else
    (void)core; (void)prio;
#endif
}
void platformPinThread(int core) {
#ifdef __SWITCH__
    // cores 0-2 for the application: 0 = render (main thread), 1 = logic, 2 = rasterization
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
#else
    (void)core;
#endif
}

#ifdef __SWITCH__
namespace {
struct StartArgs { void (*fn)(void*); void* arg; };
void threadEntry(void* a) { StartArgs sa = *(StartArgs*)a; delete (StartArgs*)a; sa.fn(sa.arg); }
}
#endif

static std::atomic<bool> g_stopping{false};
static std::mutex g_thMu;
static std::vector<std::function<void()>> g_wakes;
#ifdef __SWITCH__
static std::vector<Thread*> g_threads;
#endif
bool platformStopping() { return g_stopping.load(std::memory_order_relaxed); }
void platformOnStop(std::function<void()> wake) { std::lock_guard<std::mutex> lk(g_thMu); g_wakes.push_back(std::move(wake)); }
void platformStopThreads() {
    g_stopping = true;
    std::vector<std::function<void()>> w; { std::lock_guard<std::mutex> lk(g_thMu); w = g_wakes; }
    for (auto& f : w) f();
#ifdef __SWITCH__
    std::vector<Thread*> ts; { std::lock_guard<std::mutex> lk(g_thMu); ts.swap(g_threads); }
    for (Thread* t : ts) {
        threadWaitForExit(t);   // (each thread finishes the work at hand and exits)
        threadClose(t); delete t;
    }
    logf("helper threads stopped: %zu\n", ts.size());
#endif
}

bool platformStartThread(void (*fn)(void*), void* arg, int core, const char* name) {
#ifdef __SWITCH__
    // directly with libnx (not std::thread): this way a failure can be logged with its code and we can
    // continue with fewer threads instead of terminating the program. Priority 0x3B = that of libnx pthreads
    // (below the main thread), 256 KB stack.
    Thread* t = new Thread();
    StartArgs* sa = new StartArgs{fn, arg};
    Result rc = threadCreate(t, threadEntry, sa, nullptr, 256 * 1024, 0x3B, -2);   // the thread sets its own core
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(t);
        if (R_FAILED(rc)) threadClose(t);
        else { std::lock_guard<std::mutex> lk(g_thMu); g_threads.push_back(t); }
    }
    if (R_FAILED(rc)) {
        int used, total; platformMemInfo(&used, &total);
        logf("could not create thread '%s' (core %d): result 0x%x (%d-%04d); memory %d/%d MB\n",
             name, core, rc, 2000 + R_MODULE(rc), R_DESCRIPTION(rc), used, total);
        delete sa; delete t;
        return false;
    }
    return true;
#else
    (void)core;
    try {
        std::thread([fn, arg] { fn(arg); }).detach();
        return true;
    } catch (const std::exception& e) {
        logf("could not create thread '%s': %s\n", name, e.what());
        return false;
    }
#endif
}

namespace {
struct AsyncWriter {
    std::mutex mu; std::condition_variable cv; std::vector<std::pair<std::string, std::string>> q; bool started = false;
};
AsyncWriter* g_writer = new AsyncWriter;   // (never destroyed)
}
void platformAppendFileAsync(const std::string& path, const std::string& text) {
    {
        std::lock_guard<std::mutex> lk(g_writer->mu);
        g_writer->q.push_back({path, text});
        if (!g_writer->started) {
            g_writer->started = true;
            platformStartThread([](void*) {
                platformSetThread(0, 0x3F);   // (Switch: core 0, lowest priority)
                for (;;) {
                    std::vector<std::pair<std::string, std::string>> batch;
                    { std::unique_lock<std::mutex> lk(g_writer->mu); g_writer->cv.wait(lk, [] { return !g_writer->q.empty() || platformStopping(); }); batch.swap(g_writer->q); }
                    if (batch.empty() && platformStopping()) return;
                    for (size_t i = 0; i < batch.size(); i++) {
                        std::string text = batch[i].second;   // merge what goes to the same file
                        while (i + 1 < batch.size() && batch[i + 1].first == batch[i].first) text += batch[++i].second;
                        if (FILE* f = fopen(batch[i].first.c_str(), "ab")) { fputs(text.c_str(), f); fclose(f); }
                    }
                }
            }, nullptr, 0, "file writer");
            platformOnStop([] { std::lock_guard<std::mutex> lk(g_writer->mu); g_writer->cv.notify_all(); });
        }
    }
    g_writer->cv.notify_one();
}

void platformMemInfo(int* usedMB, int* totalMB) {
    *usedMB = *totalMB = -1;
#ifdef __SWITCH__
    u64 used = 0, total = 0;
    if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) *usedMB = (int)(used >> 20);
    if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) *totalMB = (int)(total >> 20);
#endif
}
}
