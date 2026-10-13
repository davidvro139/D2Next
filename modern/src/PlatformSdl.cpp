// The SDL3 platform: the game's window and its input, for every system SDL runs on. With the OpenGL ES / WebGL backend
// (D2MODERN_GL) the window is an OpenGL one and the backend gets the SDL_Window; otherwise, on Windows, the window's HWND
// is handed to the Direct3D backend.
#include "Platform.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED // the game has its own main (Main.cpp); SDL is told it is ready (SDL_SetMainReady)
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif
namespace d2 {
namespace {
// An SDL key as the game numbers keys (Platform.h), 0 for keys the game doesn't use.
unsigned gameKey(SDL_Keycode k) {
    if(k>=SDLK_A&&k<=SDLK_Z)return unsigned('A'+(k-SDLK_A));
    if(k>=SDLK_0&&k<=SDLK_9)return unsigned('0'+(k-SDLK_0));
    switch(k) {
    case SDLK_BACKSPACE:return key::Backspace;
    case SDLK_TAB:return key::Tab;
    case SDLK_RETURN:case SDLK_KP_ENTER:return key::Enter;
    case SDLK_LSHIFT:case SDLK_RSHIFT:return key::Shift;
    case SDLK_LALT:case SDLK_RALT:return key::Alt;
    case SDLK_ESCAPE:return key::Escape;
    case SDLK_SPACE:return key::Space;
    case SDLK_LEFT:return key::Left;
    case SDLK_UP:return key::Up;
    case SDLK_RIGHT:return key::Right;
    case SDLK_DOWN:return key::Down;
    case SDLK_DELETE:return key::Delete;
    case SDLK_F1:return key::F1;
    case SDLK_F2:return key::F2;
    case SDLK_F3:return key::F3;
    case SDLK_F4:return key::F4;
    case SDLK_F5:return key::F5;
    case SDLK_F6:return key::F6;
    case SDLK_F7:return key::F7;
    case SDLK_F8:return key::F8;
    case SDLK_GRAVE:return key::Grave;
    case SDLK_AC_BACK:return key::Escape; // Android's back button
    default:return 0;
    }
}
class SdlWindow final:public Window {
    SDL_Window* window_=nullptr;
    std::deque<InputEvent> events_;
#ifdef _WIN32
    // Test hook: WM_APP+7 with N asks for the next frame to be saved as shotN.bmp (as the Win32 window does).
    // Test hooks for driving the window from a script (SDL ignores posted mouse messages): WM_APP+8 a left button event
    // (wParam 0 move, 1 down, 2 up; lParam y<<16|x in pixels), WM_APP+9 a key (wParam a key:: code), WM_APP+10 a typed
    // character (wParam).
    static bool SDLCALL windowsMessage(void* self,MSG* msg) {
        auto& events=static_cast<SdlWindow*>(self)->events_;
        InputEvent e;
        switch(msg->message) {
        case WM_APP+7:e.type=InputEvent::Type::Shot;e.shot=int(msg->wParam);break;
        case WM_APP+8:
            e.type=msg->wParam==1?InputEvent::Type::ButtonDown:msg->wParam==2?InputEvent::Type::ButtonUp:InputEvent::Type::MouseMove;
            e.x=int(msg->lParam&0xFFFF);e.y=int((msg->lParam>>16)&0xFFFF);break;
        case WM_APP+9:e.type=InputEvent::Type::Key;e.key=unsigned(msg->wParam);break;
        case WM_APP+10:e.type=InputEvent::Type::Text;e.text=unsigned(msg->wParam);break;
        default:return true;
        }
        events.push_back(e);
        return false;
    }
#endif
    // Mouse positions come in window points; the game works in pixels (what the graphics backend draws).
    int pixels(float points) const {return int(points*SDL_GetWindowPixelDensity(window_));}
#ifdef __ANDROID__
    static constexpr bool touchScreen=true;
#else
    static constexpr bool touchScreen=false;
#endif
    bool typing_=false;
    std::vector<SDL_Gamepad*> gamepads_; // the controllers connected (opened as they appear)
    SDL_Haptic* vibrator_=nullptr;bool vibratorTried_=false; // the phone's own vibrator (opened on first use)
#ifdef __ANDROID__
    void* wifiLock_=nullptr; // a global reference to the WifiManager.WifiLock held during a TCP/IP game
#endif
    void fingerPosition(const SDL_TouchFingerEvent& f,int& x,int& y) const {
        int w=0,h=0;SDL_GetWindowSizeInPixels(window_,&w,&h);
        x=int(f.x*float(w));y=int(f.y*float(h));
    }
public:
    SdlWindow(const std::string& title,int width,int height) {
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1"); // Android's back button is Esc (the game menu), not "leave"
        // Phones: the game is played sideways, either way up. (SDL sets the activity's orientation itself and would allow
        // portrait for a resizable window.)
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
#ifdef __EMSCRIPTEN__
        SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY,"0"); // we yield in Main.cpp; SDL sleeping too aborts JSPI/Asyncify
        SDL_InitFlags init=SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD; // no haptic device in the browser
#else
        SDL_InitFlags init=SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC;
#endif
        if(!SDL_Init(init))throw std::runtime_error(std::string("SDL: ")+SDL_GetError());
        SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __EMSCRIPTEN__
        // SDL's emscripten video driver does not implement ShowWindow. A hidden window stays off-screen.
        flags|=SDL_WINDOW_OPENGL;
#else
        flags|=SDL_WINDOW_HIDDEN;
#ifdef D2MODERN_GL
        flags|=SDL_WINDOW_OPENGL;
#endif
#endif
#ifdef D2MODERN_GL
#ifdef __EMSCRIPTEN__
        // Draw into the canvas (GlBackend direct_). The canvas needs a stencil for shadows.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16);
#else
        // The backend draws into its own frame (with the stencil) and copies it here: the window needs colour only.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
#endif
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
#endif
        if(touchScreen)flags=(flags&~SDL_WINDOW_RESIZABLE)|SDL_WINDOW_FULLSCREEN; // the whole screen, without the system bars
        window_=SDL_CreateWindow(title.c_str(),width,height,flags);
        if(!window_)throw std::runtime_error(std::string("SDL window: ")+SDL_GetError());
        SDL_HideCursor(); // the game draws its own cursor (the hand from ui\cursor)
        // Typed characters always arrive on desktops; on touch screens text input brings up the on-screen keyboard, so it
        // is on only while the game asks for text (setTextInput).
        if(!touchScreen)SDL_StartTextInput(window_);
#ifdef _WIN32
        SDL_SetWindowsMessageHook(windowsMessage,this);
#endif
    }
    ~SdlWindow() override {
        lowLatencyNetwork(false);
#ifdef _WIN32
        SDL_SetWindowsMessageHook(nullptr,nullptr);
#endif
        for(auto* gamepad:gamepads_)SDL_CloseGamepad(gamepad);
        if(vibrator_)SDL_CloseHaptic(vibrator_);
        if(window_)SDL_DestroyWindow(window_);
        SDL_Quit();
    }
    bool pollEvent(InputEvent& event) override {
        SDL_Event sdl;
        while(events_.empty()&&SDL_PollEvent(&sdl)) {
            InputEvent e;
            switch(sdl.type) {
            case SDL_EVENT_QUIT:case SDL_EVENT_WINDOW_CLOSE_REQUESTED:e.type=InputEvent::Type::Quit;events_.push_back(e);break;
            case SDL_EVENT_MOUSE_MOTION:if(sdl.motion.which==SDL_TOUCH_MOUSEID)break; // fingers come as touch events
                e.type=InputEvent::Type::MouseMove;e.x=pixels(sdl.motion.x);e.y=pixels(sdl.motion.y);events_.push_back(e);break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:
                if(sdl.button.which==SDL_TOUCH_MOUSEID)break;
                if(sdl.button.button!=SDL_BUTTON_LEFT&&sdl.button.button!=SDL_BUTTON_RIGHT&&sdl.button.button!=SDL_BUTTON_MIDDLE)break;
                e.type=sdl.type==SDL_EVENT_MOUSE_BUTTON_DOWN?InputEvent::Type::ButtonDown:InputEvent::Type::ButtonUp;
                e.x=pixels(sdl.button.x);e.y=pixels(sdl.button.y);
                e.button=sdl.button.button==SDL_BUTTON_LEFT?0:sdl.button.button==SDL_BUTTON_RIGHT?1:2;
                events_.push_back(e);break;
            case SDL_EVENT_FINGER_DOWN:case SDL_EVENT_FINGER_MOTION:case SDL_EVENT_FINGER_UP:case SDL_EVENT_FINGER_CANCELED:
                // Fingers go to the game as they are; TouchControls.cpp turns them into the joystick, buttons or the mouse.
                e.type=sdl.type==SDL_EVENT_FINGER_DOWN?InputEvent::Type::TouchDown:sdl.type==SDL_EVENT_FINGER_MOTION?InputEvent::Type::TouchMove:InputEvent::Type::TouchUp;
                e.finger=int64_t(sdl.tfinger.fingerID);fingerPosition(sdl.tfinger,e.x,e.y);events_.push_back(e);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if(SDL_Gamepad* opened=SDL_OpenGamepad(sdl.gdevice.which))gamepads_.push_back(opened);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                for(auto it=gamepads_.begin();it!=gamepads_.end();++it)if(SDL_GetGamepadID(*it)==sdl.gdevice.which){SDL_CloseGamepad(*it);gamepads_.erase(it);break;}
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if(sdl.gbutton.button>=pad::Buttons)break;
                e.type=InputEvent::Type::PadButton;e.button=sdl.gbutton.button;e.pressed=sdl.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                events_.push_back(e);break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if(sdl.gaxis.axis>=pad::Axes)break;
                e.type=InputEvent::Type::PadAxis;e.button=sdl.gaxis.axis;e.value=std::clamp(float(sdl.gaxis.value)/32767.f,-1.f,1.f);
                events_.push_back(e);break;
            case SDL_EVENT_MOUSE_WHEEL:
                if(sdl.wheel.y==0)break;
                e.type=InputEvent::Type::Wheel;e.x=pixels(sdl.wheel.mouse_x);e.y=pixels(sdl.wheel.mouse_y);e.wheel=sdl.wheel.y>0?1:-1;
                events_.push_back(e);break;
            case SDL_EVENT_KEY_DOWN:
                if(const unsigned k=gameKey(sdl.key.key)){e.type=InputEvent::Type::Key;e.key=k;e.repeat=sdl.key.repeat;events_.push_back(e);}
                break;
            case SDL_EVENT_TEXT_INPUT:{
                // UTF-8 to the game's Latin-1 characters, one event each.
                const auto* s=reinterpret_cast<const unsigned char*>(sdl.text.text);
                for(size_t i=0;s[i];) {
                    unsigned c=s[i];size_t n=1;
                    if(c>=0xC0&&c<0xE0&&s[i+1]){c=((c&0x1F)<<6)|(s[i+1]&0x3F);n=2;}
                    else if(c>=0x80){while(s[i+n]&&(s[i+n]&0xC0)==0x80)++n;c='?';}
                    if(c<256){InputEvent t;t.type=InputEvent::Type::Text;t.text=c;events_.push_back(t);}
                    i+=n;
                }
                break;
            }
            default:break;
            }
        }
        if(events_.empty())return false;
        event=events_.front();events_.pop_front();return true;
    }
    void clientSize(int& width,int& height) const override {SDL_GetWindowSizeInPixels(window_,&width,&height);}
    void safeArea(int& x,int& y,int& width,int& height) const override {
        SDL_Rect safe{};
        if(!SDL_GetWindowSafeArea(window_,&safe))throw std::runtime_error(std::string("SDL safe area: ")+SDL_GetError());
        const float density=SDL_GetWindowPixelDensity(window_);
        x=int(std::ceil(float(safe.x)*density));
        y=int(std::ceil(float(safe.y)*density));
        const int right=int(std::floor(float(safe.x+safe.w)*density));
        const int bottom=int(std::floor(float(safe.y+safe.h)*density));
        width=right-x;height=bottom-y;
    }
    bool focused() const override {
#ifdef __EMSCRIPTEN__
        return true; // the emscripten video driver never sets SDL_WINDOW_INPUT_FOCUS, so the sim would stay paused
#else
        return (SDL_GetWindowFlags(window_)&SDL_WINDOW_INPUT_FOCUS)!=0;
#endif
    }
    bool keyDown(unsigned k) const override {
        const bool* state=SDL_GetKeyboardState(nullptr);
        if(k==key::Shift)return state[SDL_SCANCODE_LSHIFT]||state[SDL_SCANCODE_RSHIFT];
        if(k==key::Alt)return state[SDL_SCANCODE_LALT]||state[SDL_SCANCODE_RALT];
        if(k>='A'&&k<='Z')return state[SDL_SCANCODE_A+(k-'A')];
        if(k>='1'&&k<='9')return state[SDL_SCANCODE_1+(k-'1')];
        if(k=='0')return state[SDL_SCANCODE_0];
        return false;
    }
    void setTitle(const std::string& title) override {SDL_SetWindowTitle(window_,title.c_str());}
    void show() override {SDL_ShowWindow(window_);}
    void close() override {SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}
    void captureMouse(bool capture) override {SDL_CaptureMouse(capture);}
    void lowLatencyNetwork(bool on) override {
#ifdef __ANDROID__
        // WifiManager.createWifiLock(WIFI_MODE_FULL_LOW_LATENCY (Android 10+), else WIFI_MODE_FULL_HIGH_PERF), through JNI:
        // the Wi-Fi then stays out of its power saving, which holds packets back for up to a few hundred milliseconds.
        auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if(!env||on==(wifiLock_!=nullptr))return;
        auto failed=[&]{if(!env->ExceptionCheck())return false;env->ExceptionClear();return true;};
        if(!on) {
            auto lock=static_cast<jobject>(wifiLock_);
            jclass lockClass=env->GetObjectClass(lock);
            if(jmethodID release=env->GetMethodID(lockClass,"release","()V"))env->CallVoidMethod(lock,release);
            failed();env->DeleteLocalRef(lockClass);env->DeleteGlobalRef(lock);wifiLock_=nullptr;
            return;
        }
        auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
        if(!activity)return;
        jclass contextClass=env->FindClass("android/content/Context");
        jobject app=failed()?nullptr:env->CallObjectMethod(activity,env->GetMethodID(contextClass,"getApplicationContext","()Landroid/content/Context;"));
        jstring service=env->NewStringUTF("wifi");
        jobject wifi=failed()||!app?nullptr:env->CallObjectMethod(app,env->GetMethodID(contextClass,"getSystemService","(Ljava/lang/String;)Ljava/lang/Object;"),service);
        if(failed())wifi=nullptr;
        if(wifi) {
            jclass wifiClass=env->GetObjectClass(wifi);
            jstring tag=env->NewStringUTF("D2Next TCP/IP game");
            const jint mode=SDL_GetAndroidSDKVersion()>=29?4:3;
            jmethodID create=env->GetMethodID(wifiClass,"createWifiLock","(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;");
            jobject lock=failed()||!create?nullptr:env->CallObjectMethod(wifi,create,mode,tag);
            if(failed())lock=nullptr;
            if(lock) {
                jclass lockClass=env->GetObjectClass(lock);
                jmethodID counted=env->GetMethodID(lockClass,"setReferenceCounted","(Z)V");
                if(!failed()&&counted)env->CallVoidMethod(lock,counted,JNI_FALSE);
                jmethodID acquire=failed()?nullptr:env->GetMethodID(lockClass,"acquire","()V");
                if(acquire&&!failed()){env->CallVoidMethod(lock,acquire);if(!failed())wifiLock_=env->NewGlobalRef(lock);}
                env->DeleteLocalRef(lockClass);env->DeleteLocalRef(lock);
            }
            env->DeleteLocalRef(tag);env->DeleteLocalRef(wifiClass);env->DeleteLocalRef(wifi);
        }
        env->DeleteLocalRef(service);if(app)env->DeleteLocalRef(app);env->DeleteLocalRef(contextClass);env->DeleteLocalRef(activity);
        SDL_Log("D2Next: low-latency Wi-Fi %s",wifiLock_?"on":"not available");
#else
        (void)on;
#endif
    }
    void vibrate(float strength,int milliseconds) override {
        strength=std::clamp(strength,0.f,1.f);
        if(!gamepads_.empty()) {
            const auto motor=uint16_t(strength*65535.f);
            for(auto* gamepad:gamepads_)SDL_RumbleGamepad(gamepad,motor,motor,uint32_t(milliseconds));
            return;
        }
        if(!touchScreen)return;
        if(!vibratorTried_) {
            // Android's vibrator is a haptic device to SDL (SDLControllerManager, "VIBRATOR_SERVICE").
            vibratorTried_=true;
            int count=0;
            if(SDL_HapticID* ids=SDL_GetHaptics(&count)) {
                if(count>0&&(vibrator_=SDL_OpenHaptic(ids[0]))&&!SDL_InitHapticRumble(vibrator_)){SDL_CloseHaptic(vibrator_);vibrator_=nullptr;}
                SDL_free(ids);
            }
        }
        if(vibrator_)SDL_PlayHapticRumble(vibrator_,strength,uint32_t(milliseconds));
    }
    void setTextInput(bool typing) override {
        if(!touchScreen||typing==typing_)return;
        typing_=typing;
        if(typing)SDL_StartTextInput(window_);else SDL_StopTextInput(window_);
    }
    void* nativeHandle() const override {
#if defined(_WIN32)&&!defined(D2MODERN_GL)
        return SDL_GetPointerProperty(SDL_GetWindowProperties(window_),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
#else
        return window_;
#endif
    }
};
}
std::unique_ptr<Window> makeSdlWindow(const std::string& title,int width,int height) {
    return std::make_unique<SdlWindow>(title,width,height);
}
}
