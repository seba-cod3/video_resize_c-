#pragma once
#include <string>
#include <memory>

// Forward declarations of FFmpeg structures
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class VideoProcessor {
public:
    VideoProcessor();
    ~VideoProcessor();
    
    bool processVideo(const std::string& inputPath, const std::string& outputPath);
    void setTargetResolution(int width, int height);
    
private:
    // Target resolution
    int targetWidth = 1920;
    int targetHeight = 1080;

    // FFmpeg encoding parameters
    const int CRF = 38;
    const int AUDIO_BITRATE = 96000;
    const int THREAD_COUNT = 4;

    // Private methods for processing steps
    bool openInputFile(const std::string& inputPath);
    bool setupOutputFile(const std::string& outputPath);
    bool processFrames();
    void cleanup();

    // Calculate output dimensions maintaining aspect ratio
    void calculateOutputDimensions(int inputWidth, int inputHeight, int& outWidth, int& outHeight);
    
    // FFmpeg context variables
    AVFormatContext* inputFormatContext = nullptr;
    AVFormatContext* outputFormatContext = nullptr;
    AVCodecContext* inputVideoCodecContext = nullptr;
    AVCodecContext* outputVideoCodecContext = nullptr;
    AVCodecContext* inputAudioCodecContext = nullptr;
    AVCodecContext* outputAudioCodecContext = nullptr;
    SwsContext* swsContext = nullptr;
    
    // Stream indices
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
};
