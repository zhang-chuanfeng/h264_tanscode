#ifndef TRANSCODER_H
#define TRANSCODER_H

#include <string>
#include <chrono>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

struct TranscoderParams {
    int bitrate = 2000000;  // 默认 2Mbps
    int profile = FF_PROFILE_H264_MAIN;  // 默认 Main Profile
    int level = 41;  // 默认 Level 4.1
    int width = 0;   // 0 表示使用源视频宽度
    int height = 0;  // 0 表示使用源视频高度
    int fps = 0;     // 0 表示使用源视频帧率
};

class Transcoder {
public:
    using ProgressCallback = std::function<void(float progress)>;
    
    Transcoder() = default;
    ~Transcoder();

    bool transcode(const std::string& input_file, 
                  const std::string& output_file, 
                  const TranscoderParams& params = TranscoderParams(),
                  ProgressCallback progress_cb = nullptr);

    // 获取上一次错误信息
    std::string getLastError() const { return last_error; }

private:
    bool open_input(const std::string& input_file);
    bool open_output(const std::string& output_file);
    bool init_codec_contexts();
    bool process_frames();
    void cleanup();
    void setError(const std::string& error);

    // 视频相关
    AVFormatContext* input_ctx = nullptr;
    AVFormatContext* output_ctx = nullptr;
    AVCodecContext* video_decoder_ctx = nullptr;
    AVCodecContext* video_encoder_ctx = nullptr;
    int video_stream_index = -1;
    int out_video_stream_index = -1;

    // 音频相关
    AVCodecContext* audio_decoder_ctx = nullptr;
    AVCodecContext* audio_encoder_ctx = nullptr;
    int audio_stream_index = -1;
    int out_audio_stream_index = -1;

    // 进度回调
    ProgressCallback progress_callback = nullptr;
    int64_t total_duration = 0;
    
    // 转码参数
    TranscoderParams current_params;
    
    // 错误处理
    std::string last_error;
};

#endif // TRANSCODER_H
