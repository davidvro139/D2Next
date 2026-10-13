#pragma once
#include "Assets.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace d2 {
class VideoDecoder {
public:
    struct Pcm {Bytes samples;uint32_t rate=44100;uint16_t channels=2;};
    static constexpr uint32_t width=640,height=480;
    explicit VideoDecoder(const std::filesystem::path& file,double startSeconds=0);
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&)=delete;
    VideoDecoder& operator=(const VideoDecoder&)=delete;
    bool readFrame(Image& image);
    const std::string& error() const {return error_;}
    static std::filesystem::path findFfmpeg();
    static Pcm decodeAudio(const std::filesystem::path& file);
private:
    struct PipeCloser {void operator()(FILE* pipe) const;};
    std::unique_ptr<FILE,PipeCloser> pipe_;
    std::string error_;
};
}
