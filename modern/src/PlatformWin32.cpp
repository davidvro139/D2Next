// The Win32 platform: the game's window (messages turned into InputEvents) and text in the system font (GDI).
#include "PlatformText.h"
#include "Platform.h"
#include <windows.h>
#include <windowsx.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <deque>
#include <stdexcept>
namespace d2 {
namespace {
class Win32Window final:public Window {
    HWND window_=nullptr;
    std::deque<InputEvent> events_;
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<Win32Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Win32Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(self));}
        if(!self)return DefWindowProcW(h,m,w,l);
        auto push=[&](InputEvent e){self->events_.push_back(e);};
        const int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);
        auto mouse=[&](InputEvent::Type type,int button){InputEvent e;e.type=type;e.x=x;e.y=y;e.button=button;push(e);return 0;};
        switch(m) {
        case WM_DESTROY:PostQuitMessage(0);return 0;
        case WM_APP+7:{InputEvent e;e.type=InputEvent::Type::Shot;e.shot=int(w);push(e);return 0;}
        // The game draws its own cursor (the hand from ui\cursor), so the Windows one is hidden over the window.
        case WM_SETCURSOR:if(LOWORD(l)==HTCLIENT){SetCursor(nullptr);return TRUE;}break;
        case WM_MOUSEMOVE:return mouse(InputEvent::Type::MouseMove,0);
        case WM_LBUTTONDOWN:return mouse(InputEvent::Type::ButtonDown,0);
        case WM_LBUTTONUP:return mouse(InputEvent::Type::ButtonUp,0);
        case WM_RBUTTONDOWN:return mouse(InputEvent::Type::ButtonDown,1);
        case WM_RBUTTONUP:return mouse(InputEvent::Type::ButtonUp,1);
        case WM_MBUTTONDOWN:return mouse(InputEvent::Type::ButtonDown,2);
        case WM_MBUTTONUP:return mouse(InputEvent::Type::ButtonUp,2);
        case WM_MOUSEWHEEL:{InputEvent e;e.type=InputEvent::Type::Wheel;e.x=x;e.y=y;e.wheel=GET_WHEEL_DELTA_WPARAM(w)>0?1:-1;push(e);return 0;}
        case WM_KEYDOWN:{InputEvent e;e.type=InputEvent::Type::Key;e.key=unsigned(w);e.repeat=(l&(1LL<<30))!=0;push(e);return 0;}
        case WM_CHAR:{InputEvent e;e.type=InputEvent::Type::Text;e.text=unsigned(w);push(e);return 0;}
        default:break;
        }
        return DefWindowProcW(h,m,w,l);
    }
public:
    Win32Window(const std::string& title,int width,int height) {
        SetProcessDPIAware();
        WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"D2NextViewer"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)) throw std::runtime_error("Cannot register viewer window");
        const std::wstring wide(title.begin(),title.end());
        RECT frame{0,0,width,height};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE); // width x height inside the borders
        window_=CreateWindowW(wc.lpszClassName,wide.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,wc.hInstance,this);
        if(!window_) throw std::runtime_error("Cannot create viewer window");
    }
    ~Win32Window() override {if(window_&&IsWindow(window_))DestroyWindow(window_);}
    bool pollEvent(InputEvent& event) override {
        MSG msg{};
        while(events_.empty()&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT){InputEvent e;e.type=InputEvent::Type::Quit;events_.push_back(e);break;}
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        if(events_.empty())return false;
        event=events_.front();events_.pop_front();return true;
    }
    void clientSize(int& width,int& height) const override {RECT r{};GetClientRect(window_,&r);width=r.right;height=r.bottom;}
    bool focused() const override {return GetForegroundWindow()==window_;}
    bool keyDown(unsigned key) const override {return (GetAsyncKeyState(int(key))&0x8000)!=0;}
    void setTitle(const std::string& title) override {SetWindowTextA(window_,title.c_str());}
    void show() override {ShowWindow(window_,SW_SHOW);}
    void close() override {DestroyWindow(window_);}
    void captureMouse(bool capture) override {if(capture)SetCapture(window_);else ReleaseCapture();}
    void* nativeHandle() const override {return window_;}
};
}
std::unique_ptr<Window> makeWin32Window(const std::string& title,int width,int height) {
    return std::make_unique<Win32Window>(title,width,height);
}
Image systemTextImage(const std::string& s,std::array<uint8_t,3> color) {
    std::wstring wide(s.begin(),s.end());
    HDC dc=CreateCompatibleDC(nullptr);
    HFONT font=CreateFontW(-15,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HGDIOBJ oldFont=SelectObject(dc,font);
    SIZE size{};GetTextExtentPoint32W(dc,wide.c_str(),int(wide.size()),&size);
    size.cx+=2;size.cy+=2;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),size.cx,-size.cy,1,32,BI_RGB};
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
    SetBkColor(dc,RGB(0,0,0));SetTextColor(dc,RGB(255,255,255));
    RECT area{0,0,size.cx,size.cy};FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    TextOutW(dc,1,1,wide.c_str(),int(wide.size()));
    Image img{uint32_t(size.cx),uint32_t(size.cy),Bytes(size_t(size.cx)*size.cy*4)};
    const auto* px=static_cast<const uint8_t*>(bits);
    for(size_t i=0;i<size_t(size.cx)*size.cy;++i) {
        const uint8_t a=std::max({px[i*4],px[i*4+1],px[i*4+2]});
        img.rgba[i*4]=color[0];img.rgba[i*4+1]=color[1];img.rgba[i*4+2]=color[2];img.rgba[i*4+3]=a;
    }
    SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);
    return img;
}
void endProgram(int code) {
    std::cout.flush();std::cerr.flush();std::fflush(nullptr);
    ExitProcess(UINT(code));
}
void reportFatal(const std::string&) {} // the console shows the message
void forgetSharedGameFolder() {}
void installCrashReport() {
    // A crash prints what happened and the call stack (function names where the build has symbols) to stderr.
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG {
        const auto* record=info->ExceptionRecord;
        std::fprintf(stderr,"Crash: exception %08lX at %p\n",record->ExceptionCode,record->ExceptionAddress);
        HANDLE process=GetCurrentProcess(),thread=GetCurrentThread();
        SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES);
        SymInitialize(process,nullptr,TRUE);
        CONTEXT context=*info->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;
        frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
        frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;
        for(int depth=0;depth<40&&StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr);++depth) {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+256]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;
            DWORD64 offset=0;IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineOffset=0;
            const bool named=SymFromAddr(process,frame.AddrPC.Offset,&offset,symbol);
            const bool lined=SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineOffset,&line);
            std::fprintf(stderr,"  %2d %p %s+0x%llx",depth,reinterpret_cast<void*>(frame.AddrPC.Offset),named?symbol->Name:"?",static_cast<unsigned long long>(offset));
            if(lined)std::fprintf(stderr," (%s:%lu)",line.FileName,line.LineNumber);
            std::fprintf(stderr,"\n");
        }
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    });
}
}
