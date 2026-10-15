// Control bindings and the rebinding screen (see controls.h). Main thread.
#include "controls.h"
#include "menuui.h"
#include "platform.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace fl {

Bindings g_bind;

static const char* kActionLabel[A_COUNT] = {"MOVE UP", "MOVE DOWN", "MOVE LEFT", "MOVE RIGHT", "SHOOT UP", "SHOOT DOWN",
                                            "SHOOT LEFT", "SHOOT RIGHT", "BOMB", "ACTIVE ITEM", "CARD / PILL", "PAUSE"};
static const char* kActionKey[A_COUNT] = {"move_up", "move_down", "move_left", "move_right", "shoot_up", "shoot_down",
                                          "shoot_left", "shoot_right", "bomb", "item", "card", "pause"};

void Bindings::defaultsKb() {
    static const int d[A_COUNT] = {SDLK_w, SDLK_s, SDLK_a, SDLK_d, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_e, SDLK_SPACE, SDLK_q, SDLK_p};
    for (int a = 0; a < A_COUNT; a++) { kb[a][0] = d[a]; for (int k = 1; k < kMaxBind; k++) kb[a][k] = 0; }
    kbCustom = false;
}
void Bindings::defaultsPad() {
    for (int a = 0; a < A_COUNT; a++) for (int k = 0; k < kMaxBind; k++) pad[a][k] = -1;
    auto set = [&](int a, int i0, int i1 = -1, int i2 = -1) { pad[a][0] = i0; pad[a][1] = i1; pad[a][2] = i2; };
    set(A_MOVE_UP, SDL_CONTROLLER_BUTTON_DPAD_UP); set(A_MOVE_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    set(A_MOVE_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT); set(A_MOVE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    // face buttons by position: top shoots up, bottom down, left left, right right
    set(A_SHOOT_UP, SDL_CONTROLLER_BUTTON_Y); set(A_SHOOT_DOWN, SDL_CONTROLLER_BUTTON_A);
    set(A_SHOOT_LEFT, SDL_CONTROLLER_BUTTON_X); set(A_SHOOT_RIGHT, SDL_CONTROLLER_BUTTON_B);
#ifdef __SWITCH__
    // as the Switch build always mapped them: L bomb, ZL active item, R card/pill, + and - pause (ZR: hold to restart)
    set(A_BOMB, SDL_CONTROLLER_BUTTON_LEFTSHOULDER); set(A_ITEM, PAD_LT); set(A_CARD, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    set(A_PAUSE, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK);
#else
    set(A_BOMB, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_LT); set(A_ITEM, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_RT);
    set(A_CARD, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    set(A_PAUSE, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSTICK);
#endif
    padCustom = false; padMoveStick = 0;
}

int gameCode(int action, bool frog, bool lefty) {
    static const int arrows[4] = {38, 40, 37, 39}, wasd[4] = {87, 83, 65, 68}, zqsd[4] = {90, 83, 81, 68};
    const int* letters = frog ? zqsd : wasd;
    if (action >= A_MOVE_UP && action <= A_MOVE_RIGHT) return (lefty ? arrows : letters)[action - A_MOVE_UP];
    if (action >= A_SHOOT_UP && action <= A_SHOOT_RIGHT) return (lefty ? letters : arrows)[action - A_SHOOT_UP];
    switch (action) {
        case A_BOMB: return 69;
        case A_ITEM: return 32;
        case A_CARD: return frog ? 65 : 81;
        case A_PAUSE: return 80;
    }
    return 0;
}
int kbActionOf(int sym) {
    if (!sym) return -1;
    for (int a = 0; a < A_COUNT; a++) for (int k = 0; k < kMaxBind; k++) if (g_bind.kb[a][k] == sym) return a;
    return -1;
}
bool isGameActionCode(int c) {
    switch (c) { case 87: case 65: case 83: case 68: case 90: case 81: case 37: case 38: case 39: case 40: case 69: case 32: case 80: return true; }
    return false;
}
int padActionOf(int input) {
    if (input < 0) return -1;
    for (int a = 0; a < A_COUNT; a++) for (int k = 0; k < kMaxBind; k++) if (g_bind.pad[a][k] == input) return a;
    return -1;
}

// ---- isaac.ini
static const char* kPadIni[] = {"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
                                "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3",
                                "paddle4", "touchpad"};
static std::string padIniName(int i) {
    if (i == PAD_LT) return "lefttrigger";
    if (i == PAD_RT) return "righttrigger";
    if (i >= 0 && i < (int)(sizeof kPadIni / sizeof kPadIni[0])) return kPadIni[i];
    return "";
}
static int padFromIni(const std::string& s) {
    if (s == "lefttrigger") return PAD_LT;
    if (s == "righttrigger") return PAD_RT;
    for (int i = 0; i < (int)(sizeof kPadIni / sizeof kPadIni[0]); i++) if (s == kPadIni[i]) return i;
    return -1;
}
static std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> v; std::string cur;
    for (char ch : s) { if (ch == c) { v.push_back(cur); cur.clear(); } else cur += ch; }
    v.push_back(cur); return v;
}
static std::string trimmed(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

void bindingsLoad(const std::function<std::string(const std::string&)>& get) {
    g_bind = Bindings();
    // bind_keyboard = W;S;A;D;Up;Down;Left;Right;E;Space;Q;P   (SDL key names; alternatives joined with |)
    std::string kb = trimmed(get("bind_keyboard"));
    if (!kb.empty()) {
        auto parts = split(kb, ';');
        if (parts.size() == A_COUNT) {
            Bindings b = g_bind;
            bool ok = true;
            for (int a = 0; a < A_COUNT && ok; a++) {
                auto alts = split(parts[a], '|'); int n = 0;
                for (int k = 0; k < kMaxBind; k++) b.kb[a][k] = 0;
                for (auto& nm : alts) {
                    std::string t = trimmed(nm); if (t.empty() || n >= kMaxBind) continue;
                    SDL_Keycode sym = SDL_GetKeyFromName(t.c_str());
                    if (sym != SDLK_UNKNOWN) b.kb[a][n++] = sym;
                }
            }
            if (ok) { memcpy(g_bind.kb, b.kb, sizeof b.kb); g_bind.kbCustom = true; }
        }
    }
    std::string pd = trimmed(get("bind_controller"));
    if (!pd.empty()) {
        auto parts = split(pd, ';');
        if (parts.size() == A_COUNT) {
            for (int a = 0; a < A_COUNT; a++) {
                auto alts = split(parts[a], '|'); int n = 0;
                for (int k = 0; k < kMaxBind; k++) g_bind.pad[a][k] = -1;
                for (auto& nm : alts) { int i = padFromIni(trimmed(nm)); if (i >= 0 && n < kMaxBind) g_bind.pad[a][n++] = i; }
            }
            g_bind.padCustom = true;
        }
    }
    std::string ms = trimmed(get("controller_move_stick"));
    if (ms == "right") { g_bind.padMoveStick = 1; g_bind.padCustom = true; }
    g_bind.touchSwap = trimmed(get("touch_move_side")) == "right";
    std::string ts = trimmed(get("touch_joystick_size"));
    g_bind.touchSize = ts == "small" ? 0 : ts == "large" ? 2 : 1;
}

void bindingsSave(const std::function<void(const std::string&, const std::string&)>& set) {
    std::string kb, pd;
    if (g_bind.kbCustom)
        for (int a = 0; a < A_COUNT; a++) {
            if (a) kb += ";";
            bool first = true;
            for (int k = 0; k < kMaxBind; k++) if (g_bind.kb[a][k]) { if (!first) kb += "|"; kb += SDL_GetKeyName(g_bind.kb[a][k]); first = false; }
        }
    if (g_bind.padCustom)
        for (int a = 0; a < A_COUNT; a++) {
            if (a) pd += ";";
            bool first = true;
            for (int k = 0; k < kMaxBind; k++) if (g_bind.pad[a][k] >= 0) { if (!first) pd += "|"; pd += padIniName(g_bind.pad[a][k]); first = false; }
        }
    set("bind_keyboard", kb);
    set("bind_controller", pd);
    set("controller_move_stick", g_bind.padCustom && g_bind.padMoveStick ? "right" : "");
    set("touch_move_side", g_bind.touchSwap ? "right" : "");
    set("touch_joystick_size", g_bind.touchSize == 0 ? "small" : g_bind.touchSize == 2 ? "large" : "");
}

// ---- names
std::string keyDisplayName(int sym) {
    if (!sym) return "-";
    std::string n = SDL_GetKeyName(sym);
    if (n.empty()) return "?";
    for (auto& c : n) c = (char)toupper((unsigned char)c);
    return n;
}
std::string RebindScreen::padName(int i) const {
    static const char* gen[] = {"A", "B", "X", "Y", "BACK", "GUIDE", "START", "L3", "R3", "LB", "RB", "D-PAD UP", "D-PAD DOWN",
                                "D-PAD LEFT", "D-PAD RIGHT", "MISC", "PADDLE 1", "PADDLE 2", "PADDLE 3", "PADDLE 4", "TOUCHPAD"};
    if (padStyle_ == 1) {   // PlayStation
        static const char* ps[] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "SHARE", "PS", "OPTIONS", "L3", "R3", "L1", "R1"};
        if (i == PAD_LT) return "L2";
        if (i == PAD_RT) return "R2";
        if (i >= 0 && i <= 10) return ps[i];
    } else if (padStyle_ == 2) {   // Nintendo Switch (by position: bottom B, right A, left Y, top X)
        static const char* sw[] = {"B", "A", "Y", "X", "-", "HOME", "+", "L-STICK", "R-STICK", "L", "R"};
        if (i == PAD_LT) return "ZL";
        if (i == PAD_RT) return "ZR";
        if (i >= 0 && i <= 10) return sw[i];
    } else {
        if (i == PAD_LT) return "LT";
        if (i == PAD_RT) return "RT";
    }
    if (i >= 0 && i < (int)(sizeof gen / sizeof gen[0])) return gen[i];
    return "-";
}

// ---- rebinding screen
static const char* kDevName[DEV_COUNT] = {"KEYBOARD", "CONTROLLER", "TOUCH SCREEN"};

void RebindScreen::open(const bool avail[DEV_COUNT], int device, int padStyle) {
    for (int d = 0; d < DEV_COUNT; d++) avail_[d] = avail[d];
    if (device < 0 || device >= DEV_COUNT || !avail_[device]) { device = -1; for (int d = 0; d < DEV_COUNT; d++) if (avail_[d]) { device = d; break; } }
    if (device < 0) return;
    dev_ = device; padStyle_ = padStyle; open_ = true; capture_ = -1; pressRow_ = -3;
    build(); sel_ = std::min(1, (int)rows_.size() - 1);
    publish();
}
void RebindScreen::close() {
    if (!open_) return;
    open_ = false; capture_ = -1; saveReq_ = true;
    UiModel m; m.open = false; uiPublish(m);
}

void RebindScreen::build() {
    rows_.clear();
    rows_.push_back({0, 0});
    if (dev_ == DEV_PAD) rows_.push_back({2, 0});
    if (dev_ == DEV_TOUCH) { rows_.push_back({3, 0}); rows_.push_back({4, 0}); }
    else for (int a = 0; a < A_COUNT; a++) rows_.push_back({1, a});
    rows_.push_back({5, 0});
    rows_.push_back({6, 0});
    if (sel_ >= (int)rows_.size()) sel_ = (int)rows_.size() - 1;
}

void RebindScreen::publish() {
    UiModel m; m.open = open_;
    m.title = "CONTROLS";
    int nAvail = 0; for (int d = 0; d < DEV_COUNT; d++) nAvail += avail_[d];
    for (size_t r = 0; r < rows_.size(); r++) {
        const Row& row = rows_[r]; UiRow u;
        switch (row.kind) {
            case 0: u.label = "DEVICE"; u.value = nAvail > 1 ? std::string("< ") + kDevName[dev_] + " >" : kDevName[dev_]; break;
            case 1: {
                u.label = kActionLabel[row.arg];
                if (capture_ == row.arg) { u.value = dev_ == DEV_KEYBOARD ? "PRESS A KEY" : "PRESS A BUTTON"; break; }
                std::string v;
                for (int k = 0; k < kMaxBind; k++) {
                    if (dev_ == DEV_KEYBOARD && g_bind.kb[row.arg][k]) { if (!v.empty()) v += " / "; v += keyDisplayName(g_bind.kb[row.arg][k]); }
                    if (dev_ == DEV_PAD && g_bind.pad[row.arg][k] >= 0) { if (!v.empty()) v += " / "; v += padName(g_bind.pad[row.arg][k]); }
                }
                u.value = v.empty() ? "-" : v;
                break;
            }
            case 2: u.label = "MOVE WITH"; u.value = g_bind.padMoveStick ? "RIGHT STICK" : "LEFT STICK"; break;
            case 3: u.label = "MOVE WITH"; u.value = g_bind.touchSwap ? "RIGHT SIDE" : "LEFT SIDE"; break;
            case 4: u.label = "JOYSTICK SIZE"; u.value = g_bind.touchSize == 0 ? "SMALL" : g_bind.touchSize == 2 ? "LARGE" : "NORMAL"; break;
            case 5: u.label = "RESET TO DEFAULTS"; break;
            case 6: u.label = "DONE"; break;
        }
        m.rows.push_back(u);
    }
    m.sel = sel_;
    m.capturing = capture_ >= 0; m.blink = blink_;
    if (capture_ >= 0) m.hint = dev_ == DEV_KEYBOARD ? "PRESS THE NEW KEY  -  ESC: CANCEL" : "PRESS THE NEW BUTTON";
    else if (dev_ == DEV_KEYBOARD) m.hint = "ENTER: CHANGE    ESC: BACK";
    else if (dev_ == DEV_PAD) m.hint = padStyle_ == 1 ? "CROSS: CHANGE    CIRCLE: BACK" : padStyle_ == 2 ? "A: CHANGE    B: BACK" : "A: CHANGE    B: BACK";
    else m.hint = "TAP AN OPTION TO CHANGE IT";
    uiPublish(m);
}

static void assign(int dev, int action, int input) {
    if (dev == DEV_KEYBOARD) {
        int old[kMaxBind]; memcpy(old, g_bind.kb[action], sizeof old);
        for (int a = 0; a < A_COUNT; a++) {
            if (a == action) continue;
            bool had = false; int n = 0, keep[kMaxBind] = {0, 0, 0};
            for (int k = 0; k < kMaxBind; k++) { if (g_bind.kb[a][k] == input) had = true; else if (g_bind.kb[a][k]) keep[n++] = g_bind.kb[a][k]; }
            if (!had) continue;
            if (!n) for (int k = 0; k < kMaxBind; k++) if (old[k] && old[k] != input) { keep[n++] = old[k]; break; }   // swap
            memcpy(g_bind.kb[a], keep, sizeof keep);
        }
        g_bind.kb[action][0] = input; for (int k = 1; k < kMaxBind; k++) g_bind.kb[action][k] = 0;
        g_bind.kbCustom = true;
    } else if (dev == DEV_PAD) {
        int old[kMaxBind]; memcpy(old, g_bind.pad[action], sizeof old);
        for (int a = 0; a < A_COUNT; a++) {
            if (a == action) continue;
            bool had = false; int n = 0, keep[kMaxBind] = {-1, -1, -1};
            for (int k = 0; k < kMaxBind; k++) { if (g_bind.pad[a][k] == input) had = true; else if (g_bind.pad[a][k] >= 0) keep[n++] = g_bind.pad[a][k]; }
            if (!had) continue;
            if (!n) for (int k = 0; k < kMaxBind; k++) if (old[k] >= 0 && old[k] != input) { keep[n++] = old[k]; break; }
            memcpy(g_bind.pad[a], keep, sizeof keep);
        }
        g_bind.pad[action][0] = input; for (int k = 1; k < kMaxBind; k++) g_bind.pad[action][k] = -1;
        g_bind.padCustom = true;
    }
}

void RebindScreen::cycle(int row, int dir) {
    const Row& r = rows_[row];
    if (r.kind == 0) {
        int d = dev_;
        for (int i = 0; i < DEV_COUNT; i++) { d = (d + (dir < 0 ? DEV_COUNT - 1 : 1)) % DEV_COUNT; if (avail_[d]) break; }
        if (d != dev_) { dev_ = d; build(); }
    } else if (r.kind == 2) { g_bind.padMoveStick ^= 1; g_bind.padCustom = true; }
    else if (r.kind == 3) g_bind.touchSwap = !g_bind.touchSwap;
    else if (r.kind == 4) g_bind.touchSize = (g_bind.touchSize + (dir < 0 ? 2 : 1)) % 3;
}

void RebindScreen::activate(int row) {
    if (row < 0 || row >= (int)rows_.size()) return;
    const Row& r = rows_[row];
    switch (r.kind) {
        case 1: capture_ = r.arg; captureUntil_ = now_ + 6.0; blink_ = true; break;
        case 5:
            if (dev_ == DEV_KEYBOARD) g_bind.defaultsKb();
            else if (dev_ == DEV_PAD) g_bind.defaultsPad();
            else { g_bind.touchSwap = false; g_bind.touchSize = 1; }
            break;
        case 6: close(); return;
        default: cycle(row, 1); break;
    }
    publish();
}

void RebindScreen::nav(int code) {
    if (!open_) return;
    if (capture_ >= 0) { if (code == NAV_BACK) { capture_ = -1; publish(); } return; }
    int n = (int)rows_.size();
    switch (code) {
        case NAV_UP: sel_ = (sel_ + n - 1) % n; break;
        case NAV_DOWN: sel_ = (sel_ + 1) % n; break;
        case NAV_LEFT: case NAV_RIGHT: {
            int k = rows_[sel_].kind;
            if (k == 0 || (k >= 2 && k <= 4)) cycle(sel_, code == NAV_LEFT ? -1 : 1);
            break;
        }
        case NAV_CONFIRM: activate(sel_); return;
        case NAV_BACK: close(); return;
    }
    publish();
}

bool RebindScreen::captureKey(int sym) {
    if (!open_ || capture_ < 0 || dev_ != DEV_KEYBOARD) return false;
    if (sym != SDLK_ESCAPE) assign(DEV_KEYBOARD, capture_, sym);
    capture_ = -1; publish();
    return true;
}
bool RebindScreen::capturePad(int input) {
    if (!open_ || capture_ < 0 || dev_ != DEV_PAD) return false;
    assign(DEV_PAD, capture_, input);
    capture_ = -1; publish();
    return true;
}

void RebindScreen::pointer(int row, int type) {
    if (!open_ || capture_ >= 0) return;
    if (type == 1) {
        pressRow_ = row;
        if (row >= 0 && row != sel_) { sel_ = row; publish(); }
    } else if (type == 2) {
        int p = pressRow_; pressRow_ = -3;
        if (row != p) return;
        if (row >= 0) activate(row);
        else if (row == -2) close();
    }
}

void RebindScreen::tick(double now) {
    now_ = now;
    if (!open_ || capture_ < 0) return;
    if (now > captureUntil_) { capture_ = -1; publish(); return; }
    bool b = ((int)(now * 2.5)) % 2 == 0;
    if (b != blink_) { blink_ = b; publish(); }
}

}  // namespace fl
