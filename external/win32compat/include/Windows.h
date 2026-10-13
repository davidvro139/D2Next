#pragma once
// The part of <Windows.h> the game libraries (D2Common, D2Game, Fog, Storm, D2Lang, D2CMP, D2Hell) use, for systems
// without it: Android, Linux, the web (Emscripten). Only on the include path of non-Windows builds (CMakeLists.txt).
// Types have the Win32 sizes (DWORD is 32 bits, LONG is 32 bits even where long is 64), so file and network structures
// keep their layout.
#ifdef _WIN32
#error "win32compat is for systems without Windows.h"
#endif
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <stddef.h>
#include <limits.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

// Calling conventions and MSVC keywords mean nothing on these targets.
#define __cdecl
#define __stdcall
#define __fastcall
#define __thiscall
#define WINAPI
#define APIENTRY
#define CALLBACK
#define __forceinline inline __attribute__((always_inline))
#define __declspec(x)
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define _countof(a) (sizeof(a)/sizeof((a)[0]))

typedef uint8_t BYTE,UCHAR,BOOLEAN,*PBYTE,*LPBYTE;
typedef uint16_t WORD,USHORT,*PWORD,*LPWORD;
typedef uint32_t DWORD,UINT,ULONG,*PDWORD,*LPDWORD,*PULONG;
typedef int32_t LONG,INT,*PLONG,*LPLONG;
typedef int16_t SHORT;
typedef uint64_t ULONGLONG,DWORD64,QWORD;
typedef int64_t LONGLONG;
typedef intptr_t INT_PTR,LONG_PTR;
typedef uintptr_t UINT_PTR,ULONG_PTR,DWORD_PTR,SIZE_T;
typedef intptr_t SSIZE_T;
typedef char CHAR,*LPSTR,*PSTR;
typedef const char *LPCSTR,*PCSTR;
typedef wchar_t WCHAR,*LPWSTR,*PWSTR;
typedef const wchar_t *LPCWSTR,*PCWSTR;
typedef void VOID,*LPVOID,*PVOID,*HANDLE,*HMODULE,*HINSTANCE,*HWND,*FARPROC,*HGLOBAL,*HKEY;
typedef const void* LPCVOID;
typedef float FLOAT;
typedef LONG HRESULT;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
typedef int32_t BOOL; // as D2BasicTypes.h has it
typedef DWORD LCID;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM,LRESULT;
typedef void *HDC,*HRGN,*HBITMAP,*HFONT,*HBRUSH,*HPALETTE,*HCURSOR,*HICON,*HMENU;
typedef struct _FILETIME {DWORD dwLowDateTime,dwHighDateTime;} FILETIME,*PFILETIME,*LPFILETIME;
typedef struct _OVERLAPPED {ULONG_PTR Internal,InternalHigh;union {struct {DWORD Offset,OffsetHigh;};PVOID Pointer;};HANDLE hEvent;} OVERLAPPED,*LPOVERLAPPED;
typedef struct _SECURITY_ATTRIBUTES {DWORD nLength;LPVOID lpSecurityDescriptor;BOOL bInheritHandle;} SECURITY_ATTRIBUTES,*PSECURITY_ATTRIBUTES,*LPSECURITY_ATTRIBUTES;
typedef struct tagPALETTEENTRY {BYTE peRed,peGreen,peBlue,peFlags;} PALETTEENTRY,*PPALETTEENTRY,*LPPALETTEENTRY;
typedef struct tagRGBQUAD {BYTE rgbBlue,rgbGreen,rgbRed,rgbReserved;} RGBQUAD;
typedef struct tagPAINTSTRUCT {HDC hdc;BOOL fErase;struct {LONG left,top,right,bottom;} rcPaint;BOOL fRestore,fIncUpdate;BYTE rgbReserved[32];} PAINTSTRUCT,*PPAINTSTRUCT,*LPPAINTSTRUCT;
typedef struct tagSIZE {LONG cx,cy;} SIZE,*PSIZE,*LPSIZE;
// MSVC's code analysis annotations.
#define _Acquires_lock_(x)
#define _Releases_lock_(x)
#define _Requires_lock_held_(x)
#define _Analysis_assume_(x)
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
typedef void* HGDIOBJ;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
inline DWORD GetLastError() {return DWORD(errno);}
inline void SetLastError(DWORD error) {errno=int(error);}
#if defined(__has_builtin) && __has_builtin(__builtin_assume)
#define __assume(x) __builtin_assume(x)
#elif defined(__GNUC__)
#define __assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#define __assume(x) ((void)0)
#endif

// Text conversion as Windows does it: to UTF-16 units (wchar_t holds one unit each). CP_ACP is taken as Latin-1.
#define CP_ACP 0u
#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 0x8u
inline int MultiByteToWideChar(UINT codePage,DWORD flags,LPCSTR text,int length,wchar_t* out,int capacity) {
    const unsigned char* s=reinterpret_cast<const unsigned char*>(text);
    const size_t n=length<0?std::strlen(text)+1:size_t(length);
    int units=0;
    auto put=[&](uint32_t c) {
        if(c>=0x10000) {
            c-=0x10000;
            if(out&&units+1<capacity){out[units]=wchar_t(0xD800+(c>>10));out[units+1]=wchar_t(0xDC00+(c&0x3FF));}
            units+=2;
        } else {
            if(out&&units<capacity)out[units]=wchar_t(c);
            ++units;
        }
    };
    for(size_t i=0;i<n;) {
        uint32_t c=s[i];size_t extra=0;
        if(codePage==CP_UTF8&&c>=0x80) {
            if(c>=0xF0&&c<0xF5){extra=3;c&=0x07;}
            else if(c>=0xE0){extra=2;c&=0x0F;}
            else if(c>=0xC2&&c<0xE0){extra=1;c&=0x1F;}
            else {if(flags&MB_ERR_INVALID_CHARS){SetLastError(1113);return 0;}put(0xFFFD);++i;continue;}
            bool valid=i+extra<n;
            for(size_t k=1;valid&&k<=extra;++k){if((s[i+k]&0xC0)!=0x80)valid=false;else c=(c<<6)|(s[i+k]&0x3F);}
            if(!valid||(c>=0xD800&&c<0xE000)||c>0x10FFFF) {
                if(flags&MB_ERR_INVALID_CHARS){SetLastError(1113);return 0;}
                put(0xFFFD);++i;continue;
            }
        }
        put(c);i+=1+extra;
    }
    if(out&&units>capacity){SetLastError(122);return 0;}
    return units;
}
inline int WideCharToMultiByte(UINT codePage,DWORD,const wchar_t* text,int length,LPSTR out,int capacity,LPCSTR,BOOL*) {
    const size_t n=length<0?std::wcslen(text)+1:size_t(length);
    int bytes=0;
    auto put=[&](unsigned char b){if(out&&bytes<capacity)out[bytes]=char(b);++bytes;};
    for(size_t i=0;i<n;++i) {
        uint32_t c=uint32_t(text[i])&0xFFFFu;
        if(c>=0xD800&&c<0xDC00&&i+1<n){c=0x10000+((c-0xD800)<<10)+((uint32_t(text[i+1])&0xFFFF)-0xDC00);++i;}
        if(codePage!=CP_UTF8){put(c<256?(unsigned char)c:'?');continue;}
        if(c<0x80)put((unsigned char)c);
        else if(c<0x800){put((unsigned char)(0xC0|(c>>6)));put((unsigned char)(0x80|(c&0x3F)));}
        else if(c<0x10000){put((unsigned char)(0xE0|(c>>12)));put((unsigned char)(0x80|((c>>6)&0x3F)));put((unsigned char)(0x80|(c&0x3F)));}
        else {put((unsigned char)(0xF0|(c>>18)));put((unsigned char)(0x80|((c>>12)&0x3F)));put((unsigned char)(0x80|((c>>6)&0x3F)));put((unsigned char)(0x80|(c&0x3F)));}
    }
    if(out&&bytes>capacity){SetLastError(122);return 0;}
    return bytes;
}

// The registry: nothing is ever stored, so every value reads as missing and the game's defaults apply.
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_INVALID_PARAMETER 87L
#define HKEY_CURRENT_USER ((HKEY)(uintptr_t)0x80000001u)
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002u)
#define RRF_RT_REG_DWORD 0x10u
#define RRF_RT_REG_SZ 0x2u
inline LONG RegGetValueA(HKEY,LPCSTR,LPCSTR,DWORD,LPDWORD,PVOID,LPDWORD) {return ERROR_FILE_NOT_FOUND;}
typedef union _LARGE_INTEGER {
    struct {DWORD LowPart;LONG HighPart;};
    LONGLONG QuadPart;
} LARGE_INTEGER,*PLARGE_INTEGER;
typedef struct tagPOINT {LONG x,y;} POINT,*LPPOINT;
typedef struct tagRECT {LONG left,top,right,bottom;} RECT,*LPRECT;
typedef struct _SYSTEMTIME {WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds;} SYSTEMTIME,*LPSYSTEMTIME;

// Critical sections: recursive mutexes, as Windows' are.
typedef struct _CRITICAL_SECTION {pthread_mutex_t mutex;} CRITICAL_SECTION,*LPCRITICAL_SECTION;
inline void InitializeCriticalSection(LPCRITICAL_SECTION section) {
    pthread_mutexattr_t attributes;pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&section->mutex,&attributes);pthread_mutexattr_destroy(&attributes);
}
inline int InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION section,DWORD) {InitializeCriticalSection(section);return TRUE;}
inline void DeleteCriticalSection(LPCRITICAL_SECTION section) {pthread_mutex_destroy(&section->mutex);}
inline void EnterCriticalSection(LPCRITICAL_SECTION section) {pthread_mutex_lock(&section->mutex);}
inline int TryEnterCriticalSection(LPCRITICAL_SECTION section) {return pthread_mutex_trylock(&section->mutex)==0;}
inline void LeaveCriticalSection(LPCRITICAL_SECTION section) {pthread_mutex_unlock(&section->mutex);}

// Time.
inline DWORD GetTickCount() {
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    return DWORD(uint64_t(now.tv_sec)*1000u+uint64_t(now.tv_nsec)/1000000u);
}
inline ULONGLONG GetTickCount64() {
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    return uint64_t(now.tv_sec)*1000u+uint64_t(now.tv_nsec)/1000000u;
}
inline DWORD timeGetTime() {return GetTickCount();}
inline int QueryPerformanceFrequency(LARGE_INTEGER* frequency) {frequency->QuadPart=1000000000;return TRUE;}
inline int QueryPerformanceCounter(LARGE_INTEGER* counter) {
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    counter->QuadPart=LONGLONG(now.tv_sec)*1000000000+now.tv_nsec;return TRUE;
}
inline void Sleep(DWORD milliseconds) {usleep(useconds_t(milliseconds)*1000);}
inline void GetLocalTime(LPSYSTEMTIME out) {
    timespec now{};clock_gettime(CLOCK_REALTIME,&now);
    tm local{};time_t seconds=now.tv_sec;localtime_r(&seconds,&local);
    out->wYear=WORD(local.tm_year+1900);out->wMonth=WORD(local.tm_mon+1);out->wDayOfWeek=WORD(local.tm_wday);
    out->wDay=WORD(local.tm_mday);out->wHour=WORD(local.tm_hour);out->wMinute=WORD(local.tm_min);
    out->wSecond=WORD(local.tm_sec);out->wMilliseconds=WORD(now.tv_nsec/1000000);
}
inline void GetSystemTime(LPSYSTEMTIME out) {GetLocalTime(out);}

// Atomics (the Win32 ones are full barriers), for whatever integer the game declared (LONG, DWORD, long).
template<class T> inline T InterlockedIncrement(volatile T* value) {return __atomic_add_fetch(value,T(1),__ATOMIC_SEQ_CST);}
template<class T> inline T InterlockedDecrement(volatile T* value) {return __atomic_sub_fetch(value,T(1),__ATOMIC_SEQ_CST);}
template<class T,class V> inline T InterlockedExchange(volatile T* target,V value) {return __atomic_exchange_n(target,T(value),__ATOMIC_SEQ_CST);}
template<class T,class V> inline T InterlockedExchangeAdd(volatile T* target,V value) {return __atomic_fetch_add(target,T(value),__ATOMIC_SEQ_CST);}
template<class T,class V,class C> inline T InterlockedCompareExchange(volatile T* target,V exchange,C comparand) {
    T expected=T(comparand);__atomic_compare_exchange_n(target,&expected,T(exchange),false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return expected;
}
#define CONTAINING_RECORD(address,type,field) ((type*)((char*)(address)-offsetof(type,field)))

// Threads and processes.
inline DWORD GetCurrentThreadId() {
#if defined(__linux__) && defined(SYS_gettid)
    return DWORD(syscall(SYS_gettid));
#else
    return DWORD(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
}
inline DWORD GetCurrentProcessId() {return DWORD(getpid());}

// Messages go to the log (stderr; Android's logcat shows it through SDL).
#define MB_OK 0x0u
#define MB_ICONERROR 0x10u
#define MB_ICONSTOP 0x10u
#define MB_ICONWARNING 0x30u
#define MB_ICONEXCLAMATION 0x30u
#define MB_ICONINFORMATION 0x40u
#define MB_YESNO 0x4u
#define MB_TASKMODAL 0x2000u
#define MB_ICONHAND 0x10u
#define MB_RETRYCANCEL 0x5u
#define MB_ABORTRETRYIGNORE 0x2u
#define MB_DEFBUTTON2 0x100u
#define MB_DEFBUTTON3 0x200u
#define MB_SETFOREGROUND 0x10000u
#define MB_TOPMOST 0x40000u
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
inline int MessageBoxA(HWND,LPCSTR text,LPCSTR caption,UINT) {
    std::fprintf(stderr,"%s: %s\n",caption?caption:"",text?text:"");std::fflush(stderr);return IDOK;
}
#define MessageBox MessageBoxA
// Only a debugger sees these on Windows; the game also writes them to its own logs (Fog's Logs folder).
inline void OutputDebugStringA(LPCSTR) {}
#define OutputDebugString OutputDebugStringA
inline void DebugBreak() {__builtin_trap();}
inline void ExitProcess(UINT code) {std::_Exit(int(code));}
inline HANDLE GetCurrentProcess() {return HANDLE(intptr_t(-1));}
inline BOOL TerminateProcess(HANDLE,UINT code) {std::_Exit(int(code));}
// Code pointers are always valid here (Windows checked whether the memory was executable).
inline BOOL IsBadCodePtr(FARPROC pointer) {return pointer==nullptr;}
template<class F> inline BOOL IsBadCodePtr(F* pointer) {return pointer==nullptr;}
// The system: Windows NT 5.1 (XP), what the original game was most often run on; no CD drives.
#define VER_PLATFORM_WIN32_NT 2u
typedef struct _OSVERSIONINFOA {DWORD dwOSVersionInfoSize,dwMajorVersion,dwMinorVersion,dwBuildNumber,dwPlatformId;CHAR szCSDVersion[128];} OSVERSIONINFOA,*LPOSVERSIONINFOA;
inline BOOL GetVersionExA(LPOSVERSIONINFOA info) {
    info->dwMajorVersion=5;info->dwMinorVersion=1;info->dwBuildNumber=2600;info->dwPlatformId=VER_PLATFORM_WIN32_NT;info->szCSDVersion[0]=0;return TRUE;
}
#define DRIVE_UNKNOWN 0u
#define DRIVE_CDROM 5u
inline DWORD GetLogicalDriveStringsA(DWORD size,LPSTR buffer) {if(size)buffer[0]=0;return 0;}
inline UINT GetDriveTypeA(LPCSTR) {return DRIVE_UNKNOWN;}
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)-1)

// The heap.
#define HEAP_ZERO_MEMORY 0x8u
inline HANDLE GetProcessHeap() {return HANDLE(1);}
inline LPVOID HeapAlloc(HANDLE,DWORD flags,SIZE_T size) {return (flags&HEAP_ZERO_MEMORY)?std::calloc(1,size?size:1):std::malloc(size?size:1);}
inline LPVOID HeapReAlloc(HANDLE,DWORD,LPVOID memory,SIZE_T size) {return std::realloc(memory,size?size:1);}
inline int HeapFree(HANDLE,DWORD,LPVOID memory) {std::free(memory);return TRUE;}

// Libraries: everything is linked in.
inline HMODULE LoadLibraryA(LPCSTR) {return nullptr;}
inline int FreeLibrary(HMODULE) {return TRUE;}
inline FARPROC GetProcAddress(HMODULE,LPCSTR) {return nullptr;}
inline HMODULE GetModuleHandleA(LPCSTR) {return nullptr;}
inline DWORD GetModuleFileNameA(HMODULE,LPSTR name,DWORD size) {if(size)name[0]=0;return 0;}
#define LoadLibrary LoadLibraryA
#define GetModuleHandle GetModuleHandleA
#define GetModuleFileName GetModuleFileNameA

// The MSVC C runtime's names.
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _strcmpi strcasecmp
#define lstrcpyA strcpy
#define lstrcatA strcat
#define lstrlenA(s) int(std::strlen(s))
inline char* _strrev(char* s) {for(size_t i=0,j=std::strlen(s);i+1<j;++i,--j){const char c=s[i];s[i]=s[j-1];s[j-1]=c;}return s;}
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _strdup strdup
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _alloca __builtin_alloca
#define _rotl(v,s) ((uint32_t)(((uint32_t)(v)<<((s)&31))|((uint32_t)(v)>>((32-((s)&31))&31))))
#define _rotr(v,s) ((uint32_t)(((uint32_t)(v)>>((s)&31))|((uint32_t)(v)<<((32-((s)&31))&31))))
inline char* _strupr(char* s) {for(char* p=s;*p;++p)*p=char(std::toupper((unsigned char)*p));return s;}
inline char* _strlwr(char* s) {for(char* p=s;*p;++p)*p=char(std::tolower((unsigned char)*p));return s;}
inline char* _itoa(int value,char* buffer,int radix) {
    if(radix==10){std::sprintf(buffer,"%d",value);return buffer;}
    char digits[40];int n=0;unsigned v=unsigned(value);
    do{const unsigned d=v%unsigned(radix);digits[n++]=char(d<10?'0'+d:'a'+d-10);v/=unsigned(radix);}while(v);
    for(int i=0;i<n;++i)buffer[i]=digits[n-1-i];buffer[n]=0;return buffer;
}
#define itoa _itoa
inline char* _ultoa(unsigned long value,char* buffer,int radix) {
    char digits[72];int n=0;
    do{const unsigned d=unsigned(value%unsigned(radix));digits[n++]=char(d<10?'0'+d:'a'+d-10);value/=unsigned(radix);}while(value);
    for(int i=0;i<n;++i)buffer[i]=digits[n-1-i];buffer[n]=0;return buffer;
}
#define _lock_file flockfile
#define _unlock_file funlockfile
#define wsprintfA sprintf
inline LPSTR lstrcpynA(LPSTR destination,LPCSTR source,int size) {
    if(size<=0)return destination;
    std::strncpy(destination,source,size_t(size-1));destination[size-1]=0;return destination;
}
inline int strcpy_s(char* destination,size_t size,const char* source) {
    if(!destination||!size)return EINVAL;
    std::strncpy(destination,source?source:"",size-1);destination[size-1]=0;return 0;
}
template<size_t N> inline int strcpy_s(char (&destination)[N],const char* source) {return strcpy_s(destination,N,source);}
inline int strcat_s(char* destination,size_t size,const char* source) {
    const size_t used=strnlen(destination,size);
    if(used>=size)return EINVAL;
    std::strncat(destination,source,size-used-1);return 0;
}
template<size_t N> inline int strcat_s(char (&destination)[N],const char* source) {return strcat_s(destination,N,source);}
inline int strncpy_s(char* destination,size_t size,const char* source,size_t count) {
    if(!destination||!size)return EINVAL;
    size_t n=0;while(n<count&&n+1<size&&source[n]){destination[n]=source[n];++n;}
    destination[n]=0;return 0;
}
template<size_t N> inline int strncpy_s(char (&destination)[N],const char* source,size_t count) {return strncpy_s(destination,N,source,count);}
inline int vsprintf_s(char* buffer,size_t size,const char* format,va_list args) {return std::vsnprintf(buffer,size,format,args);}
inline int sprintf_s(char* buffer,size_t size,const char* format,...) {
    va_list args;va_start(args,format);const int n=std::vsnprintf(buffer,size,format,args);va_end(args);return n;
}
template<size_t N> inline int sprintf_s(char (&buffer)[N],const char* format,...) {
    va_list args;va_start(args,format);const int n=std::vsnprintf(buffer,N,format,args);va_end(args);return n;
}
#define _snprintf_s(buffer,size,count,...) std::snprintf(buffer,size,__VA_ARGS__)
#define _TRUNCATE ((size_t)-1)
inline int fopen_s(FILE** file,const char* name,const char* mode) {*file=std::fopen(name,mode);return *file?0:errno;}
