#include "Video.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>

namespace d2 {
namespace {
std::string stripSurroundingQuotes(std::string s) {
    while(s.size()>=2&&s.front()=='"'&&s.back()=='"')s=s.substr(1,s.size()-2);
    return s;
}
std::string quote(const std::filesystem::path& path) {
    return "\""+stripSurroundingQuotes(path.string())+"\"";
}
std::string shellCommand(const std::string& command) {
#ifdef _WIN32
    return "\""+command+" 2>NUL\"";
#else
    return command;
#endif
}
FILE* openPipe(const std::string& command) {
#ifdef _WIN32
    const auto wrapped=shellCommand(command);
    return _popen(wrapped.c_str(),"rb");
#else
    return popen(command.c_str(),"r");
#endif
}
int closePipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}
bool executableExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path,error)&&!std::filesystem::is_directory(path,error);
}
}
void VideoDecoder::PipeCloser::operator()(FILE* pipe) const {if(pipe)closePipe(pipe);}
std::filesystem::path VideoDecoder::findFfmpeg() {
    if(const char* env=std::getenv("D2MODERN_FFMPEG");env&&*env)return stripSurroundingQuotes(env);
#ifdef _WIN32
    for(const auto& path:{std::filesystem::path("C:/msys64/ucrt64/bin/ffmpeg.exe"),std::filesystem::path("C:/msys64/mingw64/bin/ffmpeg.exe"),
        std::filesystem::path("C:/Program Files/DownloadHelper CoApp/ffmpeg.exe")}) {
        if(executableExists(path))return path;
    }
    return "ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}
VideoDecoder::VideoDecoder(const std::filesystem::path& file,double startSeconds) {
#ifdef __EMSCRIPTEN__
    (void)file;(void)startSeconds;
    error_="Movies are not available in the browser build.";
    return;
#endif
    const auto ffmpeg=findFfmpeg();
    std::string command=quote(ffmpeg)+" -hide_banner -loglevel error";
    if(startSeconds>0)command+=" -ss "+std::to_string(startSeconds);
    command+=" -i "+quote(file)+
        " -an -vf \"scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:(ow-iw)/2:(oh-ih)/2\" -f rawvideo -pix_fmt rgba -";
    pipe_.reset(openPipe(command));
    if(!pipe_)error_="Could not start FFmpeg for movie decoding.";
}
VideoDecoder::~VideoDecoder()=default;
bool VideoDecoder::readFrame(Image& image) {
    if(!pipe_)return false;
    Image next{width,height,Bytes(size_t(width)*height*4)};
    size_t offset=0;
    while(offset<next.rgba.size()) {
        const size_t got=std::fread(next.rgba.data()+offset,1,next.rgba.size()-offset,pipe_.get());
        if(!got) {
            pipe_.reset();
            if(offset)error_="Movie frame ended early.";
            return false;
        }
        offset+=got;
    }
    image=std::move(next);
    return true;
}
VideoDecoder::Pcm VideoDecoder::decodeAudio(const std::filesystem::path& file) {
#ifdef __EMSCRIPTEN__
    (void)file;
    return {};
#endif
    const auto ffmpeg=findFfmpeg();
    const std::string command=quote(ffmpeg)+" -hide_banner -loglevel error -i "+quote(file)+" -vn -f s16le -ac 2 -ar 44100 -";
    std::unique_ptr<FILE,PipeCloser> pipe(openPipe(command));
    Pcm out;
    if(!pipe)return out;
    std::array<uint8_t,32768> buffer{};
    for(;;) {
        const size_t got=std::fread(buffer.data(),1,buffer.size(),pipe.get());
        if(got)out.samples.insert(out.samples.end(),buffer.begin(),buffer.begin()+long(got));
        if(got<buffer.size()) {
            if(std::feof(pipe.get()))break;
            if(std::ferror(pipe.get())){out.samples.clear();break;}
        }
    }
    return out;
}
}
