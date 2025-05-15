#include "transcoder.h"
#include <iostream>

Transcoder::~Transcoder() {
    cleanup();
}

void Transcoder::setError(const std::string& error) {
    last_error = error;
    std::cerr << "错误: " << error << std::endl;
}

bool Transcoder::transcode(const std::string& input_file, 
                         const std::string& output_file,
                         const TranscoderParams& params,
                         ProgressCallback progress_cb) {
    cleanup();
    this->progress_callback = progress_cb;  // 修复：使用this->来访问类成员
    current_params = params;

    if (!open_input(input_file)) {
        return false;
    }

    total_duration = input_ctx->duration;

    if (!open_output(output_file)) {
        return false;
    }

    if (!init_codec_contexts()) {
        return false;
    }

    // 写入输出文件头
    int ret = avformat_write_header(output_ctx, nullptr);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        setError(std::string("无法写入输出文件头: ") + err);
        return false;
    }

    if (!process_frames()) {
        return false;
    }

    // 写入输出文件尾
    av_write_trailer(output_ctx);
    cleanup();
    return true;
}

bool Transcoder::open_input(const std::string& input_file) {
    input_ctx = nullptr;
    AVDictionary* options = nullptr;
    int ret = avformat_open_input(&input_ctx, input_file.c_str(), nullptr, &options);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        setError(std::string("无法打开输入文件: ") + err);
        return false;
    }
    
    // 存储转码参数
    input_ctx->opaque = const_cast<TranscoderParams*>(&current_params);

    ret = avformat_find_stream_info(input_ctx, nullptr);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        setError(std::string("无法找到流信息: ") + err);
        return false;
    }

    // 查找视频和音频流
    video_stream_index = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_index = av_find_best_stream(input_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (video_stream_index < 0) {
        setError("找不到视频流");
        return false;
    }

    return true;
}

bool Transcoder::open_output(const std::string& output_file) {
    int ret = avformat_alloc_output_context2(&output_ctx, nullptr, nullptr, output_file.c_str());
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        setError(std::string("无法创建输出上下文: ") + err);
        return false;
    }

    // 打开输出文件
    ret = avio_open(&output_ctx->pb, output_file.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        setError(std::string("无法打开输出文件: ") + err);
        return false;
    }

    return true;
}

bool Transcoder::init_codec_contexts() {
    av_log_set_level(AV_LOG_QUIET);
    // 初始化视频编解码器
    if (video_stream_index >= 0) {
        AVStream* in_stream = input_ctx->streams[video_stream_index];
        
        // 创建输出视频流
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        AVStream* out_stream = avformat_new_stream(output_ctx, encoder);
        if (!out_stream) {
            setError("无法创建输出视频流");
            return false;
        }
        out_video_stream_index = out_stream->index;

        // 设置解码器上下文
        const AVCodec* decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
        video_decoder_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(video_decoder_ctx, in_stream->codecpar);
        if (avcodec_open2(video_decoder_ctx, decoder, nullptr) < 0) {
            setError("无法打开视频解码器");
            return false;
        }

        // 从转码参数中获取设置
        const TranscoderParams& params = *reinterpret_cast<const TranscoderParams*>(input_ctx->opaque);

        // 设置编码器上下文
        video_encoder_ctx = avcodec_alloc_context3(encoder);
        video_encoder_ctx->height = params.height > 0 ? params.height : in_stream->codecpar->height;
        video_encoder_ctx->width = params.width > 0 ? params.width : in_stream->codecpar->width;
        video_encoder_ctx->sample_aspect_ratio = in_stream->codecpar->sample_aspect_ratio;
        video_encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

        // 设置帧率和时基
        int target_fps = params.fps > 0 ? params.fps : 30;
        video_encoder_ctx->time_base = (AVRational){1, 90000};  // 使用90kHz的时基
        video_encoder_ctx->framerate = (AVRational){target_fps, 1};

        // 设置输出流的参数
        out_stream->time_base = video_encoder_ctx->time_base;
        out_stream->r_frame_rate = video_encoder_ctx->framerate;
        out_stream->avg_frame_rate = video_encoder_ctx->framerate;

        // 设置编码参数
        video_encoder_ctx->bit_rate = params.bitrate;
        video_encoder_ctx->gop_size = video_encoder_ctx->framerate.num;  // 一秒一个关键帧
        video_encoder_ctx->max_b_frames = 2;
        video_encoder_ctx->profile = params.profile;
        video_encoder_ctx->level = params.level;
        
        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            video_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // 打开编码器
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "preset", "medium", 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
        av_dict_set(&opts, "verbose", "-1", 0);

        if (avcodec_open2(video_encoder_ctx, encoder, &opts) < 0) {
            setError("无法打开视频编码器");
            return false;
        }

        av_dict_free(&opts);

        // 复制编码器参数到输出流
        avcodec_parameters_from_context(out_stream->codecpar, video_encoder_ctx);
    }

    // 初始化音频编解码器
    if (audio_stream_index >= 0) {
        AVStream* in_stream = input_ctx->streams[audio_stream_index];
        
        // 创建输出音频流
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        AVStream* out_stream = avformat_new_stream(output_ctx, encoder);
        if (!out_stream) {
            setError("无法创建输出音频流");
            return false;
        }
        out_audio_stream_index = out_stream->index;

        // 设置解码器上下文
        const AVCodec* decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
        audio_decoder_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(audio_decoder_ctx, in_stream->codecpar);
        if (avcodec_open2(audio_decoder_ctx, decoder, nullptr) < 0) {
            setError("无法打开音频解码器");
            return false;
        }

        // 设置编码器上下文
        audio_encoder_ctx = avcodec_alloc_context3(encoder);
        audio_encoder_ctx->sample_fmt = encoder->sample_fmts[0];
        audio_encoder_ctx->channel_layout = in_stream->codecpar->channel_layout;
        audio_encoder_ctx->channels = in_stream->codecpar->channels;
        audio_encoder_ctx->sample_rate = in_stream->codecpar->sample_rate;
        audio_encoder_ctx->time_base = (AVRational){1, audio_encoder_ctx->sample_rate};
        
        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(audio_encoder_ctx, encoder, nullptr) < 0) {
            setError("无法打开音频编码器");
            return false;
        }

        // 复制编码器参数到输出流
        avcodec_parameters_from_context(out_stream->codecpar, audio_encoder_ctx);
    }

    return true;
}

bool Transcoder::process_frames() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* encoded_frame = av_frame_alloc();

    while (av_read_frame(input_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            // 处理视频帧
            int ret = avcodec_send_packet(video_decoder_ctx, packet);
            if (ret < 0) {
                setError("发送视频包到解码器失败");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(video_decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    setError("从解码器接收视频帧失败");
                    break;
                }

                // 转换时间戳
                if (frame->pts != AV_NOPTS_VALUE) {
                    frame->pts = av_rescale_q_rnd(frame->pts,
                        input_ctx->streams[video_stream_index]->time_base,
                        video_encoder_ctx->time_base,
                        static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                } else {
                    // 如果没有pts，使用递增的pts
                    static int64_t next_pts = 0;
                    frame->pts = next_pts++;
                }

                // 编码帧
                ret = avcodec_send_frame(video_encoder_ctx, frame);
                if (ret < 0) {
                    setError("发送视频帧到编码器失败");
                    break;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(video_encoder_ctx, packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        setError("从编码器接收视频包失败");
                        break;
                    }

                    // 写入输出文件
                    packet->stream_index = out_video_stream_index;
                    ret = av_interleaved_write_frame(output_ctx, packet);
                    if (ret < 0) {
                        setError("写入视频帧失败");
                        break;
                    }
                }
            }

            // 报告进度
            if (progress_callback) {
                static std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
                static float last_progress = -1.0f;
                static int64_t total_frames = 0;
                static int64_t processed_frames = 0;
                
                // 初始化总帧数
                if (total_frames == 0) {
                    AVStream* stream = input_ctx->streams[video_stream_index];
                    if (stream->nb_frames > 0) {
                        total_frames = stream->nb_frames;
                    } else {
                        // 如果没有总帧数信息，使用文件时长和帧率估算
                        AVRational frame_rate = av_guess_frame_rate(input_ctx, stream, NULL);
                        if (frame_rate.num > 0 && frame_rate.den > 0) {
                            total_frames = (stream->duration * frame_rate.num) / 
                                (frame_rate.den * (int64_t)AV_TIME_BASE);
                        } else {
                            // 如果无法获取帧率，使用默认值
                            total_frames = 1000; // 默认值，避免除以零
                        }
                    }
                    // std::cerr << "DEBUG: 总帧数 = " << total_frames << std::endl;
                }
                
                processed_frames++; // 移动到这里，确保每个解码的视频帧都被计数
                
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);
                
                // 每100毫秒更新一次进度
                if (elapsed.count() >= 100) {
                    float progress = (float)(processed_frames * 100.0 / total_frames);
                    
                    // 确保进度在有效范围内
                    progress = std::min(std::max(progress, 0.0f), 100.0f);
                    
                    // std::cerr << "DEBUG: 已处理帧数=" << processed_frames 
                    //          << ", 总帧数=" << total_frames 
                    //          << ", 进度=" << progress << "%" << std::endl;
                    
                    // 只有当进度变化超过0.1%时才更新
                    if (progress - last_progress >= 0.1f || progress >= 99.9f) {
                        progress_callback(progress);
                        last_progress = progress;
                        last_update = now;
                    }
                }
            }
        }
        else if (packet->stream_index == audio_stream_index && audio_encoder_ctx) {
            // 处理音频帧
            int ret = avcodec_send_packet(audio_decoder_ctx, packet);
            if (ret < 0) {
                setError("发送音频包到解码器失败");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audio_decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    setError("从解码器接收音频帧失败");
                    break;
                }

                // 转换时间戳
                frame->pts = av_rescale_q(frame->pts,
                    input_ctx->streams[audio_stream_index]->time_base,
                    audio_encoder_ctx->time_base);

                // 编码帧
                ret = avcodec_send_frame(audio_encoder_ctx, frame);
                if (ret < 0) {
                    setError("发送音频帧到编码器失败");
                    break;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(audio_encoder_ctx, packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        setError("从编码器接收音频包失败");
                        break;
                    }

                    // 写入输出文件
                    packet->stream_index = out_audio_stream_index;
                    ret = av_interleaved_write_frame(output_ctx, packet);
                    if (ret < 0) {
                        setError("写入音频帧失败");
                        break;
                    }
                }
            }
        }

        av_packet_unref(packet);
    }

    // 清理
    av_frame_free(&frame);
    av_frame_free(&encoded_frame);
    av_packet_free(&packet);

    // 确保显示100%进度
    if (progress_callback) {
        progress_callback(100.0f);
    }

    return true;
}

void Transcoder::cleanup() {
    if (video_decoder_ctx)
        avcodec_free_context(&video_decoder_ctx);
    if (video_encoder_ctx)
        avcodec_free_context(&video_encoder_ctx);
    if (audio_decoder_ctx)
        avcodec_free_context(&audio_decoder_ctx);
    if (audio_encoder_ctx)
        avcodec_free_context(&audio_encoder_ctx);
    
    if (input_ctx)
        avformat_close_input(&input_ctx);
    
    if (output_ctx) {
        if (output_ctx->pb)
            avio_closep(&output_ctx->pb);
        avformat_free_context(output_ctx);
        output_ctx = nullptr;
    }
}
