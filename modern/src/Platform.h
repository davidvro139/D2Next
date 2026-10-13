#pragma once
#include <cstdint>
#include <memory>
#include <string>
namespace d2 {
// Keys as the game uses them: letters and digits are their ASCII capitals ('W', '1'); the others are named here. (The
// numbers are the ones Windows gives these keys, so the Win32 window passes its key codes on unchanged.)
namespace key {
constexpr unsigned Backspace=0x08,Tab=0x09,Enter=0x0D,Shift=0x10,Escape=0x1B,Space=0x20,
    Alt=0x12,Left=0x25,Up=0x26,Right=0x27,Down=0x28,Delete=0x2E,F1=0x70,F2=0x71,F3=0x72,F4=0x73,
    F5=0x74,F6=0x75,F7=0x76,F8=0x77,Grave=0xC0;
}
// A game controller's buttons and axes, numbered as SDL numbers them (SDL_GamepadButton, SDL_GamepadAxis): the face
// buttons by position (A is the bottom one), whatever is printed on them.
namespace pad {
constexpr int A=0,B=1,X=2,Y=3,Back=4,Guide=5,Start=6,LeftStick=7,RightStick=8,LB=9,RB=10,Up=11,Down=12,Left=13,Right=14,Buttons=15;
enum Axis {LeftX=0,LeftY=1,RightX=2,RightY=3,LT=4,RT=5,Axes=6};
}
// Something the player did, or the window was asked to do. Positions are window pixels.
struct InputEvent {
    enum class Type:uint8_t {
        MouseMove,  // x, y
        ButtonDown, // x, y, button (0 left, 1 right, 2 middle)
        ButtonUp,
        Wheel,      // x, y, wheel (+1 away from the player, -1 towards)
        Key,        // key (key:: or a capital), repeat when held down
        Text,       // text: a typed character (Latin-1)
        Quit,       // the window was closed
        Shot,       // test hook: save the next frame as shot<shot>.bmp in D2MODERN_SHOT_DIR
        TouchDown,  // finger, x, y: a finger on a touch screen (TouchControls.cpp decides what it does)
        TouchMove,
        TouchUp,
        PadButton,  // button (pad::), pressed: a game controller's button went down or up
        PadAxis     // button (pad::Axis), value: a stick (-1..1, down and right positive) or trigger (0..1) moved
    };
    Type type=Type::MouseMove;
    int x=0,y=0,button=0;
    int64_t finger=0;
    bool pressed=false;
    float value=0;
    unsigned key=0,text=0;
    bool repeat=false;
    int wheel=0,shot=0;
};
// The game's window: events, size, held keys. Made with the size of its drawing area (inside the borders). Win32 (PlatformWin32.cpp) and SDL3 (PlatformSdl.cpp) implement it.
class Window {
public:
    virtual ~Window()=default;
    // The next event, if there is one (pending system messages are handled first).
    virtual bool pollEvent(InputEvent& event)=0;
    virtual void clientSize(int& width,int& height) const=0;
    // The window area safe for interactive content (excluding cutouts and system gesture areas).
    virtual void safeArea(int& x,int& y,int& width,int& height) const {
        clientSize(width,height);
        x=0;y=0;
    }
    virtual bool focused() const=0;
    virtual bool keyDown(unsigned key) const=0; // held right now (WASD walking, shift)
    virtual void setTitle(const std::string& title)=0;
    virtual void show()=0;
    virtual void close()=0; // a Quit event follows
    virtual void captureMouse(bool capture)=0; // keep mouse events while a button drags outside the window
    // Whether the player is typing (a character's name, an amount of gold): touch screens show their keyboard.
    virtual void setTextInput(bool typing) {(void)typing;}
    // A short shake (strength 0..1): the controllers' rumble, or the phone's vibrator when no controller is connected.
    virtual void vibrate(float strength,int milliseconds) {(void)strength;(void)milliseconds;}
    // During a TCP/IP game: keeps the Wi-Fi out of its power saving, which holds packets back and sends them in bursts
    // (Android's low-latency Wi-Fi lock; nothing elsewhere).
    virtual void lowLatencyNetwork(bool on) {(void)on;}
    virtual void* nativeHandle() const=0; // for the graphics backend (an HWND on Windows)
};
void installCrashReport(); // a crash prints its call stack to stderr
// The game stops on an error: where there is no console to read it from (Android), it is shown to the player.
void reportFatal(const std::string& message);
void forgetSharedGameFolder(); // Android: drop the saved shared-folder choice. Other platforms: no-op.
[[noreturn]] void endProgram(int code); // flushes the output and ends the process without running global destructors
std::unique_ptr<Window> makeWin32Window(const std::string& title,int width,int height);
std::unique_ptr<Window> makeSdlWindow(const std::string& title,int width,int height); // built with D2MODERN_PLATFORM=SDL
// The window of the build's platform.
inline std::unique_ptr<Window> makeWindow(const std::string& title,int width,int height) {
#ifdef D2MODERN_SDL
    return makeSdlWindow(title,width,height);
#else
    return makeWin32Window(title,width,height);
#endif
}
}
