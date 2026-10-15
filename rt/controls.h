// Control bindings (key rebinding) and the in-game rebinding screen. Main thread.
//
// The game only knows fixed Flash key codes (WASD, arrows, E, Space, Q, P; Z/Q/A in AZERTY mode; movement and
// shooting swapped in "lefty" mode). Bindings say which physical key / controller button performs each ACTION;
// once the player customizes a device, its inputs are translated to the code the game expects for that action
// with its current lefty/AZERTY settings. Devices that were never customized keep the original behaviour.
//
// Controller inputs use SDL's GameController numbering on every platform (0 = bottom face button ... 14 = D-pad
// right; 100/101 = left/right trigger). On Switch the Joy-Con buttons are mapped to them by position.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace fl {

enum Action { A_MOVE_UP, A_MOVE_DOWN, A_MOVE_LEFT, A_MOVE_RIGHT, A_SHOOT_UP, A_SHOOT_DOWN, A_SHOOT_LEFT, A_SHOOT_RIGHT,
              A_BOMB, A_ITEM, A_CARD, A_PAUSE, A_COUNT };
enum Device { DEV_KEYBOARD, DEV_PAD, DEV_TOUCH, DEV_COUNT };
enum { PAD_LT = 100, PAD_RT = 101 };
constexpr int kMaxBind = 3;

struct Bindings {
    int kb[A_COUNT][kMaxBind];    // SDL keycodes (0 = empty slot)
    int pad[A_COUNT][kMaxBind];   // controller inputs (-1 = empty slot)
    bool kbCustom = false, padCustom = false;
    int padMoveStick = 0;         // 0: left stick moves, right stick shoots; 1: swapped
    bool touchSwap = false;       // touch: movement joystick on the right half
    int touchSize = 1;            // touch joysticks: 0 small, 1 normal, 2 large
    Bindings() { defaultsKb(); defaultsPad(); }
    void defaultsKb();
    void defaultsPad();
};
extern Bindings g_bind;

// Flash key code the game reads for an action, given its AZERTY ("frog") and lefty settings
int gameCode(int action, bool frog, bool lefty);
// keyboard: action bound to an SDL keycode (-1 none); is this Flash code one of the game's action keys?
int kbActionOf(int sym);
bool isGameActionCode(int flashCode);
// controller: action bound to an input (-1 none)
int padActionOf(int input);

// isaac.ini: read (get returns "" for a missing key) / write (set(key, value); "" = default)
void bindingsLoad(const std::function<std::string(const std::string&)>& get);
void bindingsSave(const std::function<void(const std::string&, const std::string&)>& set);

// ---- rebinding screen
struct UiModel;
class RebindScreen {
public:
    // available devices, the one to show first, controller naming (0 generic A/B/X/Y, 1 PlayStation, 2 Switch)
    void open(const bool avail[DEV_COUNT], int device, int padStyle);
    void close();
    bool isOpen() const { return open_; }
    bool capturing() const { return capture_ >= 0; }
    int device() const { return dev_; }
    // navigation from any device: 0 up 1 down 2 left 3 right 4 confirm 5 back
    void nav(int code);
    // raw inputs while capturing (return true if taken)
    bool captureKey(int sym);         // keyboard tab (Escape cancels)
    bool capturePad(int input);       // controller tab
    // pointer (mouse / touch) on row r (-1: on the panel but no row, -2: outside the panel); type 1 down, 2 up
    void pointer(int row, int type);
    void tick(double now);            // capture timeout, blinking
    bool takeSaveRequest() { bool r = saveReq_; saveReq_ = false; return r; }
private:
    struct Row { int kind; int arg; };   // kind: 0 device, 1 action, 2 stick side, 3 touch side, 4 touch size, 5 reset, 6 done
    bool open_ = false, avail_[DEV_COUNT] = {}, saveReq_ = false;
    int dev_ = DEV_KEYBOARD, sel_ = 0, capture_ = -1, padStyle_ = 0, pressRow_ = -3;
    double captureUntil_ = 0, now_ = 0; bool blink_ = false;
    std::vector<Row> rows_;
    void build();
    void publish();
    void activate(int row);
    void cycle(int row, int dir);
    std::string padName(int input) const;
};
std::string keyDisplayName(int sym);

}  // namespace fl
