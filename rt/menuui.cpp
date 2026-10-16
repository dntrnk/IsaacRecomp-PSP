// Controller menu navigation, the "REBIND CONTROLS" entry and the rebinding screen inside the game (see menuui.h).
//
// The game's menus are Flash buttons made for the mouse (and a few keys). With a controller, the logic thread
// keeps a "focused" button among the ones on screen: the D-pad / stick moves it to the nearest button in that
// direction, A (cross) clicks it exactly like the mouse would, B (circle) goes back. The renderer draws an outline
// around it. The rebinding screen and its entry on the Control options page are built from shapes and text fields
// synthesized at run time (the text in the game's own HUD font) and placed in the display list, so they are drawn,
// scaled and layered like the rest of the game.
#include "menuui.h"
#include "core.h"
#include "display.h"
#include "snapshot.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <cctype>
#include <cstdlib>
#include "platform.h"

namespace fl {

void visibleButtons(std::vector<ButtonObj*>& out);
ButtonObj* topButtonAt(double x, double y);
int g_rebindRequests = 0;
uint16_t g_synthCharBase = 0xffff;   // first synthesized character id (the renderer doesn't learn them for preloading)

// ============================================================ shared model (main thread -> logic thread)
static std::mutex g_uiMu;
static UiModel g_uiModel; static int g_uiVersion = 0;
static std::vector<RectF> g_uiRows; static RectF g_uiPanel; static bool g_uiShown = false;

void uiPublish(const UiModel& m) { std::lock_guard<std::mutex> lk(g_uiMu); g_uiModel = m; g_uiVersion++; }
int uiHitRow(double sx, double sy) {
    std::lock_guard<std::mutex> lk(g_uiMu);
    if (!g_uiShown) return -2;
    for (size_t i = 0; i < g_uiRows.size(); i++) { const RectF& r = g_uiRows[i]; if (sx >= r.x0 && sx <= r.x1 && sy >= r.y0 && sy <= r.y1) return (int)i; }
    if (sx >= g_uiPanel.x0 && sx <= g_uiPanel.x1 && sy >= g_uiPanel.y0 && sy <= g_uiPanel.y1) return -1;
    return -2;
}

// ============================================================ synthesized shapes and texts
static std::map<std::string, uint16_t> g_synth;
static uint16_t addChar(CharDef* d) {
    // character ids from the top of the 16-bit range down (the SWF's own ids are far below)
    static int next = 0xfffe;
    if (g_movie.chars.size() < 0x10000) g_movie.chars.resize(0x10000, nullptr);
    while (next > 0 && g_movie.chars[next]) next--;
    if (next <= 0) return 0;
    d->id = (uint16_t)next; g_movie.chars[next] = d;
    if (next < g_synthCharBase) g_synthCharBase = (uint16_t)next;
    return (uint16_t)next--;
}
static uint16_t rectShape(float w, float h, uint32_t argb) {
    char key[96]; snprintf(key, sizeof key, "r%.1f,%.1f,%08x", w, h, argb);
    auto it = g_synth.find(key); if (it != g_synth.end()) return it->second;
    auto* s = new ShapeDef(); s->type = CH_SHAPE;
    s->bounds = {0, 0, w, h};
    ShapeLayer L; FillStyle f; f.type = 0; f.argb = argb; L.fills.push_back(f);
    Path p; p.verbs = {0, 1, 1, 1, 1}; p.pts = {0, 0, w, 0, w, h, 0, h, 0, 0};
    L.fillPaths.push_back(p);
    s->layers.push_back(L);
    return g_synth[key] = addChar(s);
}
static uint16_t hudFontId() {
    static int id = -1;
    if (id >= 0) return (uint16_t)id;
    id = 0;
    for (CharDef* c : g_movie.chars)
        if (c && c->type == CH_FONT && ((FontDef*)c)->name.compare(0, 8, "Upheaval") == 0) { id = c->id; break; }
    return (uint16_t)id;
}
static uint16_t textField(float w, float size, uint32_t argb, int align) {
    char key[96]; snprintf(key, sizeof key, "t%.1f,%.1f,%08x,%d", w, size, argb, align);
    auto it = g_synth.find(key); if (it != g_synth.end()) return it->second;
    auto* e = new EditTextDef(); e->type = CH_EDITTEXT;
    e->bounds = {0, 0, w, size * 1.4};
    e->fontId = hudFontId(); e->height = size; e->argb = argb; e->align = (uint8_t)align;
    e->readOnly = true; e->noSelect = true; e->useOutlines = true; e->hasText = true;
    return g_synth[key] = addChar(e);
}
#include "menuui_glyphs.inc"
// A word written with the hand-drawn glyphs as one shape (strokes with round ends, like a marker). H = cap height.
static uint16_t handwritten(const char* text, float H, uint32_t argb, float* outW = nullptr) {
    char key[160]; snprintf(key, sizeof key, "h%s|%.1f|%08x", text, H, argb);
    const float sw = 0.17f * H, spacing = 0.085f;
    auto it = g_synth.find(key);
    auto glyph = [](char c) -> const HandGlyph* {
        for (auto& g : kHandGlyphs) if (g.c == c) return &g;
        for (auto& g : kHandGlyphs) if (g.c == (char)tolower((unsigned char)c)) return &g;
        return nullptr;
    };
    float x = 0;
    for (const char* p = text; *p; p++) if (const HandGlyph* g = glyph(*p)) x += (g->w + spacing) * H;
    if (outW) *outW = x;
    if (it != g_synth.end()) return it->second;
    auto* s = new ShapeDef(); s->type = CH_SHAPE;
    ShapeLayer L; LineStyle ls; ls.width = sw; ls.argb = argb; ls.startCap = ls.endCap = 0; ls.join = 0; ls.noClose = true;
    L.lines.push_back(ls);
    Path path; uint32_t seed = 7;
    auto jitter = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) % 1000 / 1000.0f - 0.5f) * 0.03f; };
    x = 0;
    for (const char* p = text; *p; p++) {
        const HandGlyph* g = glyph(*p); if (!g) continue;
        float rot = jitter() * 1.2f, dy = jitter() * 0.6f;   // each letter a little off, as if written by hand
        bool start = true;
        for (int i = 0; i < g->n; i++) {
            float gx = g->pts[2 * i], gy = g->pts[2 * i + 1];
            if (std::isnan(gx)) { start = true; continue; }
            float px = gx + jitter(), py = gy + jitter() + dy;
            px += (py - 0.5f) * rot;
            path.verbs.push_back(start ? 0 : 1); path.pts.push_back(x + px * H); path.pts.push_back(py * H);
            start = false;
        }
        x += (g->w + spacing) * H;
    }
    L.linePaths.push_back(path);
    s->bounds = {-sw, -sw, x + sw, H + sw};
    s->layers.push_back(L);
    return g_synth[key] = addChar(s);
}
static DisplayObject* put(MovieClip* parent, uint16_t id, int depth, double x, double y, const char* name = nullptr) {
    if (!id) return nullptr;
    PlaceCmd pc; pc.hasMatrix = true; pc.m.tx = x; pc.m.ty = y;
    DisplayObject* d = instantiate(parent, id, depth, &pc, false);
    if (d && name) d->name = intern(name);
    return d;
}
static DisplayObject* putText(MovieClip* parent, int depth, double x, double y, float w, float size, uint32_t argb, int align, const std::string& text) {
    DisplayObject* d = put(parent, textField(w, size, argb, align), depth, x, y);
    if (d && d->kind == OK_EDITTEXT) ((EditTextObj*)d)->setText(text);
    return d;
}

// ============================================================ "REBIND CONTROLS" on the Control options page
// The options sheet (_root.paus: _root.pauss in the menus, _root.hud.paus during a run) shows "Control options" on
// its frame 8 in the three games: "Lefty Flip" and "Azerty" toggles, then empty paper. The entry goes below them.
static DisplayObject* g_rebindLabel = nullptr;
static bool g_labelPressed = false;
static void removeLabel() {
    if (g_rebindLabel) { if (!g_rebindLabel->removed && g_rebindLabel->parent) removeObject(g_rebindLabel); g_rebindLabel = nullptr; }
}
static MovieClip* optionsSheet() {
    Value p = getMemK(Value((Object*)vm.root), intern("paus"));
    if (!p.isObj() || p.o->kind != OK_CLIP) return nullptr;
    MovieClip* c = (MovieClip*)p.o;
    if (c->removed) return nullptr;
    for (DisplayObject* q = c; q; q = q->parent) if (!q->visible) return nullptr;
    return c;
}
static void updateRebindLabel() {
    MovieClip* sheet = optionsSheet();
    bool want = sheet && sheet->cur == 8;
    if (!want || (g_rebindLabel && (g_rebindLabel->removed || g_rebindLabel->parent != sheet))) { removeLabel(); if (!want) return; }
    if (g_rebindLabel) return;
    // in the sheet's own coordinates: the two toggles' labels start at x ~ 73, rows at y ~ 76 and ~ 123;
    // the free line below them is at ~ 205 (above the gamepad doodle)
    // written in the same place and size as the other two labels: their capitals start at x ~ 73, the rows are
    // ~ 44 apart (Lefty Flip at y ~ 66, Azerty at ~ 110) and capitals are ~ 26 tall
    g_rebindLabel = put(sheet, handwritten("Rebind Controls", 26, 0xff6a6662u), DEPTH_BIAS + 9000, 74, 155);
    if (g_rebindLabel) g_rebindLabel->name = intern("__rebind");
}
static bool labelHit(double sx, double sy) {
    if (!g_rebindLabel || g_rebindLabel->removed) return false;
    RectF r = stageBounds(g_rebindLabel);
    double pad = g_stageH / 120.0;
    return r.valid() && sx >= r.x0 - pad && sx <= r.x1 + pad && sy >= r.y0 - pad && sy <= r.y1 + pad;
}
void menuMouse(double sx, double sy, int type) {
    if (type == 1) g_labelPressed = labelHit(sx, sy);
    else if (type == 2) { if (g_labelPressed && labelHit(sx, sy)) g_rebindRequests++; g_labelPressed = false; }
}

// ============================================================ rebinding screen (drawn in the game's display list)
static MovieClip* g_ui = nullptr; static int g_uiBuilt = -1;
static void removeUi() {
    if (g_ui) { if (!g_ui->removed && g_ui->parent) removeObject(g_ui); g_ui = nullptr; }
    std::lock_guard<std::mutex> lk(g_uiMu); g_uiShown = false; g_uiRows.clear();
}
static void buildUi(const UiModel& m) {
    removeUi();
    if (!m.open) return;
    MovieClip* root = vm.root;
    g_ui = createEmptyMovieClip(root, intern("__controls"), 1000000);
    if (!g_ui) return;
    // _root's visible area (the stage, in _root coordinates: the HD patch scales _root)
    Mat inv; if (!root->m.invert(inv)) return;
    double rx0, ry0, rx1, ry1; inv.apply(0, 0, rx0, ry0); inv.apply(g_stageW, g_stageH, rx1, ry1);
    const int n = (int)m.rows.size();
    const double W = 480, RH = 21.5, top = 50, bottom = 34, H = top + n * RH + bottom;
    double cx = (rx0 + rx1) / 2, cy = (ry0 + ry1) / 2, x0 = std::round(cx - W / 2), y0 = std::round(cy - H / 2);
    int dp = 1;
    put(g_ui, rectShape((float)(rx1 - rx0 + 2), (float)(ry1 - ry0 + 2), 0xa0000000u), dp++, rx0 - 1, ry0 - 1);   // dim the game
    put(g_ui, rectShape((float)W + 6, (float)H + 6, 0x60000000u), dp++, x0 + 4, y0 + 6);                      // shadow
    put(g_ui, rectShape((float)W + 6, (float)H + 6, 0xff0c0c0cu), dp++, x0 - 3, y0 - 3);                     // border
    put(g_ui, rectShape((float)W, (float)H, 0xffe2dbcfu), dp++, x0, y0);                                       // paper
    putText(g_ui, dp++, x0, y0 + 9, (float)W, 26, 0xff8c2a2au, 2, m.title);
    put(g_ui, rectShape((float)W - 40, 2, 0xff8d8780u), dp++, x0 + 20, y0 + top - 6);
    std::vector<RectF> rows;
    for (int i = 0; i < n; i++) {
        double ry = y0 + top + i * RH;
        bool sel = i == m.sel;
        if (sel) put(g_ui, rectShape((float)W - 24, (float)RH - 1, 0xff4a4744u), dp++, x0 + 12, ry);
        uint32_t lc = sel ? 0xffebe5dbu : 0xff4a4744u, vc = sel ? 0xfff0c674u : 0xff8c2a2au;
        putText(g_ui, dp++, x0 + 18, ry + 1, 220, 15, lc, 0, m.rows[i].label);
        std::string v = m.rows[i].value;
        if (sel && m.capturing && !m.blink) v.clear();
        if (!v.empty()) putText(g_ui, dp++, x0 + W - 18 - 260, ry + 1, 260, 15, vc, 1, v);
        double ax0, ay0, ax1, ay1;
        root->m.apply(x0 + 8, ry, ax0, ay0); root->m.apply(x0 + W - 8, ry + RH, ax1, ay1);
        rows.push_back({std::min(ax0, ax1), std::min(ay0, ay1), std::max(ax0, ax1), std::max(ay0, ay1)});
    }
    putText(g_ui, dp++, x0, y0 + H - 26, (float)W, 12, 0xff66625du, 2, m.hint);
    double px0, py0, px1, py1; root->m.apply(x0, y0, px0, py0); root->m.apply(x0 + W, y0 + H, px1, py1);
    std::lock_guard<std::mutex> lk(g_uiMu);
    g_uiRows = rows; g_uiPanel = {std::min(px0, px1), std::min(py0, py1), std::max(px0, px1), std::max(py0, py1)}; g_uiShown = true;
}

// ============================================================ controller navigation
static bool g_navOn = false;
static DisplayObject* g_focus = nullptr;
static double g_lastFx = -1, g_lastFy = -1;
struct Pulse { int code; uint32_t until; };
static std::vector<Pulse> g_pulses;
static bool g_uiOpen = false, g_inGame = false, g_gameplay = false;
static bool g_focusShown = false; static RectF g_focusRect;

struct Cand { DisplayObject* d; RectF r; double cx, cy; };
static DisplayObject* topLevel(DisplayObject* d) { while (d->parent && d->parent != vm.root) d = d->parent; return d; }
static std::vector<Cand> candidates() {
    std::vector<Cand> out;
    std::vector<ButtonObj*> bs; visibleButtons(bs);
    std::vector<Cand> all;
    for (ButtonObj* b : bs) {
        if (!b->enabled || b->removed) continue;
        double a = 1; for (DisplayObject* p = b; p; p = p->parent) a *= p->cx.am;
        if (a < 0.05) continue;
        RectF r = stageBounds(b);
        if (!r.valid() || r.x1 - r.x0 < 4 || r.y1 - r.y0 < 4) continue;
        double cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
        if (cx < 0 || cy < 0 || cx > g_stageW || cy > g_stageH) continue;
        all.push_back({b, r, cx, cy});
    }
    if (g_rebindLabel && !g_rebindLabel->removed) {
        RectF r = stageBounds(g_rebindLabel);
        if (r.valid()) all.push_back({g_rebindLabel, r, (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2});
    }
    if (all.empty()) return out;
    // only the topmost layer: the options sheet over the main menu, the pause sheet over the room...
    int best = -1 << 30;
    for (auto& c : all) best = std::max(best, topLevel(c.d)->depth);
    for (auto& c : all) {
        if (topLevel(c.d)->depth != best) continue;
        bool dup = false;
        for (auto& o : out) if (std::fabs(o.cx - c.cx) < 6 && std::fabs(o.cy - c.cy) < 6) { o = c; dup = true; break; }
        if (!dup) out.push_back(c);
    }
    return out;
}
static const Cand* findFocus(const std::vector<Cand>& cs) {
    for (auto& c : cs) if (c.d == g_focus) return &c;
    return nullptr;
}
static void hover(const Cand& c) {
    if (c.d->kind == OK_BUTTON) mouseEvent(c.cx, c.cy, 0);
    g_lastFx = c.cx; g_lastFy = c.cy;
}
static const Cand* ensureFocus(const std::vector<Cand>& cs) {
    if (cs.empty()) { g_focus = nullptr; return nullptr; }
    if (const Cand* f = findFocus(cs)) return f;
    const Cand* pick = &cs[0];
    if (g_lastFx >= 0) {   // the screen changed: the option nearest to where the outline was
        double bd = 1e30;
        for (auto& c : cs) { double d = std::hypot(c.cx - g_lastFx, c.cy - g_lastFy); if (d < bd) { bd = d; pick = &c; } }
    } else {               // first time: the biggest option (Start, the pause sheet's Options...)
        double ba = -1;
        for (auto& c : cs) { double a = (c.r.x1 - c.r.x0) * (c.r.y1 - c.r.y0); if (a > ba) { ba = a; pick = &c; } }
    }
    g_focus = pick->d; hover(*pick);
    return pick;
}
static void pulse(int code, int ascii) {
    keyEvent(code, ascii, true);
    g_pulses.push_back({code, g_player.tick + 3});
}
static void goBack() {
    if (g_inGame && !g_gameplay) {
        MovieClip* sheet = optionsSheet();
        if (!sheet || sheet->cur == 2) { pulse(80, 0); return; }   // the pause screen itself: resume
    }
    pulse(27, 27);   // the game's "Back" buttons answer to Escape
}
static void click(const Cand& c) {
    if (c.d == g_rebindLabel) { g_rebindRequests++; return; }
    double x = c.cx, y = c.cy;
    if (topButtonAt(x, y) != c.d) {   // the center may be outside the hit shape: look for a point inside
        bool found = false;
        for (int j = 1; j <= 5 && !found; j++) for (int i = 1; i <= 5 && !found; i++) {
            double px = c.r.x0 + (c.r.x1 - c.r.x0) * i / 6, py = c.r.y0 + (c.r.y1 - c.r.y0) * j / 6;
            if (topButtonAt(px, py) == c.d) { x = px; y = py; found = true; }
        }
    }
    if (getenv("ISAAC_INPUT_LOG")) logf("[nav] click %s at %.0f,%.0f (hit %d)\n", targetPath(c.d)->s.c_str(), x, y, (int)(topButtonAt(x, y) == c.d));
    mouseEvent(x, y, 0); mouseEvent(x, y, 1); mouseEvent(x, y, 2);
}

void padNavEvent(int code) {
    if (code == NAV_OFF) { g_navOn = false; return; }
    if (g_uiOpen || g_gameplay) return;
    g_navOn = true;
    std::vector<Cand> cs = candidates();
    if (cs.empty()) {   // a screen without buttons (cutscenes, title cards...): the keys it expects
        static const int arrows[4] = {38, 40, 37, 39};
        if (code <= NAV_RIGHT) pulse(arrows[code], 0);
        else if (code == NAV_CONFIRM) pulse(32, 32);
        else if (code == NAV_BACK) goBack();
        return;
    }
    const Cand* f = findFocus(cs);
    bool fresh = !f;
    if (getenv("ISAAC_INPUT_LOG")) logf("[nav] event %d: %d candidates, focus %p %s\n", code, (int)cs.size(), (void*)g_focus, fresh ? "lost" : "kept");
    if (!f) f = ensureFocus(cs);
    if (code == NAV_CONFIRM) { if (!fresh || cs.size() == 1) click(*f); return; }
    if (code == NAV_BACK) { goBack(); return; }
    if (fresh) return;   // the first press only shows where the outline is
    static const double dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    double dx = dirs[code][0], dy = dirs[code][1];
    // nearest option in that direction: first among those inside a 45-degree cone (or overlapping the current
    // one sideways, e.g. the next word on the same line), then anything ahead. Distances between the facing edges,
    // so a wide option next to a narrow one doesn't look farther than it is.
    auto gap = [&](const Cand& c) {   // distance along the direction between the facing edges (can be < 0 if they overlap)
        if (dx > 0) return c.r.x0 - f->r.x1; if (dx < 0) return f->r.x0 - c.r.x1;
        if (dy > 0) return c.r.y0 - f->r.y1; return f->r.y0 - c.r.y1;
    };
    auto sideGap = [&](const Cand& c) {   // perpendicular separation (0 if their spans overlap)
        double a0 = dx ? f->r.y0 : f->r.x0, a1 = dx ? f->r.y1 : f->r.x1, b0 = dx ? c.r.y0 : c.r.x0, b1 = dx ? c.r.y1 : c.r.x1;
        return std::max(0.0, std::max(b0 - a1, a0 - b1));
    };
    const Cand* best = nullptr; double bs = 1e30;
    for (int pass = 0; pass < 2 && !best; pass++)
        for (auto& c : cs) {
            if (c.d == f->d) continue;
            double vx = c.cx - f->cx, vy = c.cy - f->cy;
            double along = vx * dx + vy * dy, side = std::fabs(vx * dy - vy * dx);
            if (along < 4) continue;
            double sg = sideGap(c);
            if (pass == 0 && sg > 0 && side > along) continue;   // outside the cone and not on the same line
            if (pass == 1 && side > along * 2) continue;          // (never sideways)
            double s = std::max(0.0, gap(c)) + along * 0.25 + sg * 2.0;
            if (s < bs) { bs = s; best = &c; }
        }
    if (best) { g_focus = best->d; hover(*best); }
}

// ============================================================ per tick
void menuUiTick(bool gameplay, bool inGame) {
    static bool rooted = false;
    if (!rooted) { rooted = true; gcAddRoot((GcObj**)&g_rebindLabel); gcAddRoot((GcObj**)&g_ui); gcAddRoot((GcObj**)&g_focus); }
    g_gameplay = gameplay; g_inGame = inGame;
    for (size_t i = 0; i < g_pulses.size();) {
        if (g_player.tick >= g_pulses[i].until) { keyEvent(g_pulses[i].code, 0, false); g_pulses.erase(g_pulses.begin() + i); }
        else i++;
    }
    UiModel m; int ver;
    { std::lock_guard<std::mutex> lk(g_uiMu); ver = g_uiVersion; if (ver != g_uiBuilt) m = g_uiModel; }
    if (ver != g_uiBuilt) { g_uiBuilt = ver; g_uiOpen = m.open; buildUi(m); }
    else if (g_ui && (g_ui->removed || !g_ui->parent)) { g_ui = nullptr; g_uiBuilt = -1; }   // (the game cleared _root: rebuild)
    g_focusShown = false;
    if (gameplay && !g_uiOpen) { removeLabel(); return; }
    updateRebindLabel();
    if (!g_navOn || g_uiOpen) return;
    std::vector<Cand> cs = candidates();
    const Cand* f = ensureFocus(cs);
    if (!f) return;
    g_focusShown = true; g_focusRect = f->r;
}

void menuUiFill(Snapshot& s) {
    s.rebindRequests = g_rebindRequests;
    Value so = getMemK(Value((Object*)vm.root), intern("so"));
    Value data = so.isObj() ? getMemK(so, intern("data")) : VUNDEF;
    if (data.isObj()) { s.frog = truthy(getMemK(data, intern("frog"))); s.lefty = truthy(getMemK(data, intern("left"))); }
    s.focusOn = g_focusShown;
    s.focusUid = g_focusShown && g_focus ? g_focus->uid : 0;
    if (!g_focusShown) return;
    double pad = g_stageH / 160.0;
    s.focusRect[0] = (float)(g_focusRect.x0 - pad); s.focusRect[1] = (float)(g_focusRect.y0 - pad);
    s.focusRect[2] = (float)(g_focusRect.x1 + pad); s.focusRect[3] = (float)(g_focusRect.y1 + pad);
}

}  // namespace fl
