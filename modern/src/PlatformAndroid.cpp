// Android: SDL's Java activity loads libSDL3.so and libmain.so (this file). The game's files live in private app storage;
// a user-selected SAF folder holds the MPQs in place and mirrors saves, settings, and logs for access outside the app.
#include "Platform.h"
#include "ProgramPath.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <condition_variable>
#include <jni.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
int gameMain(int argc,wchar_t** argv); // Main.cpp
namespace d2 {
void reportFatal(const std::string& message) {
    const std::string text=message+"\n\n(Also in d2next.log in the D2Next folder.)";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"D2Next stopped",text.c_str(),nullptr);
}
void forgetSharedGameFolder() {
    auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env||!activity)return;
    jclass activityClass=env->GetObjectClass(activity);
    if(!activityClass||env->ExceptionCheck()){if(env->ExceptionCheck())env->ExceptionClear();return;}
    jmethodID method=env->GetMethodID(activityClass,"forgetSharedGameFolder","()V");
    if(!method||env->ExceptionCheck()){if(env->ExceptionCheck())env->ExceptionClear();env->DeleteLocalRef(activityClass);return;}
    env->CallVoidMethod(activity,method);
    if(env->ExceptionCheck())env->ExceptionClear();
    env->DeleteLocalRef(activityClass);
}
}
namespace {
std::mutex syncMutex;
std::condition_variable syncCondition;
bool syncFinished=false;
bool syncSucceeded=false;
std::string syncMessage;

void reportStartupError(const std::string& message) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"D2Next startup: %s",message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"D2Next needs its game files",message.c_str(),nullptr);
}

void checkJavaException(JNIEnv* env,const char* operation) {
    if(!env->ExceptionCheck())return;
    env->ExceptionDescribe();
    env->ExceptionClear();
    throw std::runtime_error(std::string("Android could not ")+operation+".");
}

bool callFolderSync(const char* methodName,const std::filesystem::path* directory,std::string& message) {
    auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env||!activity) {
        message="Android storage access is unavailable.";
        return false;
    }
    jclass activityClass=env->GetObjectClass(activity);
    checkJavaException(env,"open the D2Next activity");
    const char* signature=directory?"(Ljava/lang/String;)V":"()V";
    jmethodID method=env->GetMethodID(activityClass,methodName,signature);
    checkJavaException(env,"find the D2Next folder access service");
    if(!method) {
        env->DeleteLocalRef(activityClass);
        message="D2Next folder access is unavailable.";
        return false;
    }
    jstring javaPath=nullptr;
    if(directory) {
        const std::string path=directory->string();
        javaPath=env->NewStringUTF(path.c_str());
        checkJavaException(env,"prepare D2Next folder access");
        if(!javaPath) {
            env->DeleteLocalRef(activityClass);
            message="Could not prepare the private D2Next folder.";
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(syncMutex);
        syncFinished=false;
        syncSucceeded=false;
        syncMessage.clear();
    }
    if(javaPath)env->CallVoidMethod(activity,method,javaPath);
    else env->CallVoidMethod(activity,method);
    if(javaPath)env->DeleteLocalRef(javaPath);
    env->DeleteLocalRef(activityClass);
    checkJavaException(env,"open or synchronize the D2Next folder");
    std::unique_lock<std::mutex> lock(syncMutex);
    syncCondition.wait(lock,[] { return syncFinished; });
    message=syncMessage;
    return syncSucceeded;
}
}

extern "C" JNIEXPORT void JNICALL
Java_org_d2next_game_D2NextActivity_nativeFolderSyncFinished(JNIEnv* env,jclass,jboolean success,jstring message) {
    const char* utf=message?env->GetStringUTFChars(message,nullptr):nullptr;
    {
        std::lock_guard<std::mutex> lock(syncMutex);
        syncSucceeded=success==JNI_TRUE;
        syncMessage=utf?utf:"Android folder synchronization did not return a status message.";
        syncFinished=true;
    }
    if(utf)env->ReleaseStringUTFChars(message,utf);
    syncCondition.notify_one();
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int,char**) {
    const char* storage=SDL_GetAndroidInternalStoragePath();
    if(!storage) {
        reportStartupError("Android did not provide D2Next's private storage folder.");
        return 1;
    }
    const std::filesystem::path folder=storage;
    std::error_code error;
    for(const auto& subfolder:{"MPQ","Save","Logs"}) {
        std::filesystem::create_directories(folder/subfolder,error);
        if(error) {
            reportStartupError("Cannot create D2Next's private folder:\n"+(folder/subfolder).string()+"\n\n"+error.message());
            return 1;
        }
    }
    d2::setProgramDirectory(folder);
    d2::setEnvironment("D2NEXT_SAVE_DIR",(folder/"Save").string().c_str());
    const auto logPath=folder/"d2next.log",prevLog=folder/"d2next.prev.log";
    std::error_code logError;
    std::filesystem::remove(prevLog,logError);
    logError.clear();
    if(std::filesystem::exists(logPath,logError))std::filesystem::rename(logPath,prevLog,logError);
    if(std::FILE* log=std::fopen(logPath.c_str(),"w")) {
        dup2(fileno(log),STDOUT_FILENO);dup2(fileno(log),STDERR_FILENO);
        std::setvbuf(stdout,nullptr,_IOLBF,4096);std::setvbuf(stderr,nullptr,_IONBF,0);
    }
    SDL_Log("D2Next: private game folder %s",folder.c_str());
    std::string message;
    bool synchronized=false;
    try {
        synchronized=callFolderSync("requestLauncher",&folder,message);
    } catch(const std::exception& exception) {
        message=exception.what();
    }
    if(!synchronized) {
        if(message=="Folder access was cancelled."||message=="The shared D2Next folder is missing required Diablo II archives.")
            return 1;
        if(message.empty())
            reportStartupError("Choose or create a D2Next folder using Android's folder picker. Suggested location: Documents/D2Next. It will contain MPQ, Save, and Logs.");
        else
            reportStartupError(message);
        return 1;
    }
    const auto mpq=folder/"MPQ";
    std::string missing;
    for(const char* name:{"d2data.mpq","d2exp.mpq","d2char.mpq","d2sfx.mpq","Patch_D2.mpq"}) {
        error.clear();
        if(!std::filesystem::exists(d2::gameFile(mpq,name),error)) {
            if(error) {
                reportStartupError("Cannot access a game archive in:\n"+mpq.string()+"\n\n"+error.message());
                return 1;
            }
            missing+=std::string("\n  ")+name;
        }
    }
    if(!missing.empty()) {
        reportStartupError("The shared D2Next folder is missing required Diablo II archives in its MPQ folder:"+
            missing+"\n\nCopy them from your own Diablo II 1.10 / Lord of Destruction installation into that MPQ folder "
            "(see README.txt there). Suggested location: Documents/D2Next/MPQ. Then open D2Next again and tap Launch.");
        return 1;
    }
    std::vector<std::wstring> arguments={L"D2Next",L"--native"};
    std::vector<wchar_t*> pointers;
    for(auto& argument:arguments)pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    const int result=gameMain(int(arguments.size()),pointers.data());
    std::fflush(stdout);
    std::fflush(stderr);
    try {
        if(!callFolderSync("syncStorageFolder",nullptr,message))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Could not mirror D2Next files to the selected folder: %s",message.c_str());
    } catch(const std::exception& exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Could not mirror D2Next files to the selected folder: %s",exception.what());
    }
    return result;
}
