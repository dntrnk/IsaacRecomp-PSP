// Recomp-specific tweaks to the original game (not part of the SWF):
//  - "mdm" object (Steam's Zinc wrapper): quitting the game and fullscreen actually work;
//  - options menu without the options that depended on Zinc/Flash and do nothing here.
#include "display.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>

namespace fl {

// ---------------------------------------------------------------- subpaths of a shape
struct SubPath { size_t v0, v1, p0, p1; float x0, y0, x1, y1; };
static std::vector<SubPath> splitPath(const Path& p) {
    std::vector<SubPath> out; size_t k = 0;
    for (size_t i = 0; i < p.verbs.size(); i++) {
        uint8_t v = p.verbs[i];
        if (v == 0) { if (!out.empty()) { out.back().v1 = i; out.back().p1 = k; } out.push_back({i, 0, k, 0, 1e9f, 1e9f, -1e9f, -1e9f}); }
        int n = v == 2 ? 4 : 2;
        if (!out.empty()) for (int j = 0; j < n; j += 2) {
            SubPath& s = out.back(); float x = p.pts[k + j], y = p.pts[k + j + 1];
            s.x0 = std::min(s.x0, x); s.y0 = std::min(s.y0, y); s.x1 = std::max(s.x1, x); s.y1 = std::max(s.y1, y);
        }
        k += n;
    }
    if (!out.empty()) { out.back().v1 = p.verbs.size(); out.back().p1 = k; }
    return out;
}
void debugShapeSubpaths(uint16_t id) {
    CharDef* cd = g_movie.get(id); if (!cd || cd->type != CH_SHAPE) return;
    ShapeDef* s = (ShapeDef*)cd;
    for (size_t l = 0; l < s->layers.size(); l++) for (size_t f = 0; f < s->layers[l].fillPaths.size(); f++)
        for (auto& sp : splitPath(s->layers[l].fillPaths[f]))
            logf("[sub] layer %zu fill %zu  x %.1f..%.1f  y %.1f..%.1f\n", l, f, sp.x0, sp.x1, sp.y0, sp.y1);
}

// ---------------------------------------------------------------- mdm object (Zinc)
std::atomic<int> g_fullscreenRequest{-1};   // -1 none; 0/1 = the game requests windowed / fullscreen
std::atomic<bool> g_isFullscreen{false};    // actual state (updated by the main thread)
Value mdmExit(Act&, Value, int, Value*) { g_player.quit = true; return VUNDEF; }
// "Delete save" (Collection) deletes the save and, in the Steam version, also its "serial.txt" copy. The
// already-computed stats, collection and character screens keep showing the old data until a restart,
// so after deleting, the game is restarted from scratch (main.cpp: g_restartRequest).
std::atomic<bool> g_restartRequest{false};
Value mdmDeleteFile(Act&, Value, int argc, Value* argv) {
    if (argc > 0 && toStr(argv[0])->s.find("serial.txt") != std::string::npos) {
        logf("save deleted: restarting the game\n");
        g_restartRequest = true; g_player.quit = true;
    }
    return VUNDEF;
}
static bool g_fsSynced = false;
Value mdmFull(Act&, Value, int argc, Value* argv) {
    // at startup the game applies its saved setting: isaac.ini's takes precedence (and is synced in recompPatchesTick)
    if (g_fsSynced) { g_fullscreenRequest = argc > 0 && truthy(argv[0]) ? 1 : 0; if (g_trace) logf("[mdm] showFullScreen(%d)\n", g_fullscreenRequest.load()); }
    return VUNDEF;
}

// ---------------------------------------------------------------- per-game ids
// All three versions share these graphics (same shapes, different ids): options menu (sprite), shape with
// the labels, shape with the fullscreen note, and controls screen.
struct GameIds { const char* tag; uint16_t optSprite, optLabels, optNote, controls; };
static const GameIds kIds[] = {
    {"wotl",    1374, 1258, 1253, 1337},
    {"og",       944,  863,  858,  942},
    {"eternal", 1422, 1306, 1301, 1385},
};
static const GameIds* ids() {
    if (!g_game) return nullptr;
    for (auto& g : kIds) if (!strcmp(g.tag, g_game->tag)) return &g;
    return nullptr;
}

// ---------------------------------------------------------------- options menu
// Options sprite (1374 in WotL), frame 5. Shape 1258 contains all the labels; "Detail", "FullScreen
// Resolutions" and "Window size" are removed and the remaining rows are moved up. Sprite-local coordinates.
static const float kFullDy = -60, kMuteDy = -40;
static void patchOptionsShape() {
    CharDef* cd = g_movie.get(ids()->optLabels); if (!cd || cd->type != CH_SHAPE) return;
    ShapeDef* s = (ShapeDef*)cd;
    for (auto& L : s->layers) for (auto& P : L.fillPaths) {
        if (P.empty()) continue;
        Path out;
        for (auto& sp : splitPath(P)) {
            float dy = 0;
            if (sp.y1 < 95) continue;                                  // Detail / FullScreen Resolutions
            if (sp.y0 >= 194 && sp.y1 <= 218.5f) continue;              // Window size
            if (sp.y0 >= 150 && sp.y1 <= 194) dy = kFullDy;             // FullScreen:
            else if (sp.y0 >= 218.5f) dy = kMuteDy;                     // Mute: / Music:
            out.verbs.insert(out.verbs.end(), P.verbs.begin() + sp.v0, P.verbs.begin() + sp.v1);
            for (size_t k = sp.p0; k < sp.p1; k += 2) { out.pts.push_back(P.pts[k]); out.pts.push_back(P.pts[k + 1] + dy); }
        }
        P = std::move(out);
    }
    s->hitPolys.clear(); s->hitBuilt = false;
}
// the note "(press F anytime to change to full)" is in shape 1253 (fill 2): it moves up with its row
static void patchOptionsNote() {
    CharDef* cd = g_movie.get(ids()->optNote); if (!cd || cd->type != CH_SHAPE) return;
    ShapeDef* s = (ShapeDef*)cd;
    if (s->layers.empty() || s->layers[0].fillPaths.size() < 3) return;
    Path& P = s->layers[0].fillPaths[2];
    for (auto& sp : splitPath(P))
        if (sp.x0 >= 270 && sp.y0 >= 140 && sp.y1 <= 192)
            for (size_t k = sp.p0; k < sp.p1; k += 2) P.pts[k + 1] += kFullDy;
    s->hitPolys.clear(); s->hitBuilt = false;
}
// controls screen (shape 1337): "Game pad?: use JoyToKey (google it!)" no longer applies, the recomp
// supports controllers directly
static void patchControlsShape() {
    CharDef* cd = g_movie.get(ids()->controls); if (!cd || cd->type != CH_SHAPE) return;
    ShapeDef* s = (ShapeDef*)cd;
    for (size_t l = 0; l < s->layers.size() && l < 2; l++) for (auto& P : s->layers[l].fillPaths) {
        if (P.empty()) continue;
        Path out;
        for (auto& sp : splitPath(P)) {
            if (l == 0 && sp.y0 >= 190) continue;
            if (l == 1 && sp.y0 >= 228 && sp.x0 >= 205) continue;
            out.verbs.insert(out.verbs.end(), P.verbs.begin() + sp.v0, P.verbs.begin() + sp.v1);
            out.pts.insert(out.pts.end(), P.pts.begin() + sp.p0, P.pts.begin() + sp.p1);
        }
        P = std::move(out);
    }
    s->hitPolys.clear(); s->hitBuilt = false;
}
void applyLoadPatches() { if (ids()) { patchOptionsShape(); patchOptionsNote(); patchControlsShape(); } }

static void setY(DisplayObject* d, double y) { if (d && std::fabs(d->m.ty - y) > 1e-6) { d->m.ty = y; d->scriptXform = true; } }
static void tweakOptions(MovieClip* p) {
    const GameIds* g = ids();
    if (!g || !p || p->kind != OK_CLIP || !p->def || p->def->id != g->optSprite || p->cur != 5) return;
    for (DisplayObject* k : p->kids) {
        const std::string n = k->name ? k->name->s : std::string();
        if ((n.size() == 3 && n[0] == 'b' && n[1] == 'q') || (n.size() == 2 && n[0] == 'r' && n[1] >= '0' && n[1] <= '9') ||
            (n.size() == 3 && n[0] == 'w' && n[1] == 'q')) k->visible = false;
        else if (n == "b0") setY(k, 158.8 + kFullDy);
        else if (n == "b1") setY(k, 227.6 + kMuteDy);
        else if (n == "b2") setY(k, 224.6 + kMuteDy);
    }
}

// every tick, after the logic
void recompPatchesTick() {
    Value root((Object*)vm.root);
    if (!g_fsSynced && g_player.tick >= 2) {   // the menu's "FullScreen" checkbox reflects the actual state
        Value so = getMemK(root, intern("so"));
        Value data = so.isObj() ? getMemK(so, intern("data")) : VUNDEF;
        if (data.isObj()) { setMemK(data, intern("full"), Value::boolean(g_isFullscreen.load())); g_fsSynced = true; }
    }
    Value paus = getMemK(root, intern("paus"));
    if (paus.isObj() && paus.o->kind == OK_CLIP) tweakOptions((MovieClip*)paus.o);
    // Eternal Edition: the HUD's hard-mode mark is set with `hud.hardon._visible = _root.hardmode`. With a save
    // that never toggled hard mode, hardmode is undefined; in this runtime (as in Ruffle) assigning undefined to
    // _visible changes nothing, so the mark stayed visible. Hide it whenever hard mode is not on.
    if (g_game && !strcmp(g_game->tag, "eternal")) {
        Value hud = getMemK(root, intern("hud"));
        Value mark = hud.isObj() ? getMemK(hud, intern("hardon")) : VUNDEF;
        if (mark.isObj() && mark.o->isDisplay() && !truthy(getMemK(root, intern("hardmode")))) ((DisplayObject*)mark.o)->visible = false;
    }
}
// actively playing: _root.a is the game clip and it is not paused (unpause = true)
bool recompInGameplay(bool* inGame) {
    Value a = getMemK(Value((Object*)vm.root), intern("a"));
    bool g = a.isObj() && getMemK(a, intern("player")).isObj();
    if (inGame) *inGame = g;
    return g && truthy(getMemK(a, intern("unpause")));
}
// the player toggled fullscreen with F11 / Alt+Enter (logic thread)
void recompFullscreenChanged(bool full) {
    Value root((Object*)vm.root);
    Value so = getMemK(root, intern("so"));
    Value data = so.isObj() ? getMemK(so, intern("data")) : VUNDEF;
    if (data.isObj()) setMemK(data, intern("full"), Value::boolean(full));
    Value paus = getMemK(root, intern("paus"));
    if (paus.isObj() && paus.o->kind == OK_CLIP && ((MovieClip*)paus.o)->cur == 5) {
        Act ac{vm.globalScope, VUNDEF, vm.root, nullptr};
        callMethodK(ac, root, intern("showit"), 0, nullptr);
        runActions();
    }
}

}  // namespace fl
