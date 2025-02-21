#include "video_processor.hpp"
#include <stdexcept>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
}

VideoProcessor::VideoProcessor() {}

VideoProcessor::~VideoProcessor() {
    cleanup();
}

void VideoProcessor::cleanup() {
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    
    if (inputVideoCodecContext) {
        avcodec_free_context(&inputVideoCodecContext);
    }
    if (outputVideoCodecContext) {
        avcodec_free_context(&outputVideoCodecContext);
    }
    if (inputAudioCodecContext) {
        avcodec_free_context(&inputAudioCodecContext);
    }
    if (outputAudioCodecContext) {
        avcodec_free_context(&outputAudioCodecContext);
    }
    
    if (inputFormatContext) {
        avformat_close_input(&inputFormatContext);
    }
    if (outputFormatContext) {
        if (outputFormatContext->pb) {
            avio_closep(&outputFormatContext->pb);
        }
        avformat_free_context(outputFormatContext);
    }
}

void VideoProcessor::calculateOutputDimensions(int inputWidth, int inputHeight, int& outWidth, int& outHeight) {
    if (inputWidth <= targetWidth && inputHeight <= targetHeight) {
        outWidth = inputWidth;
        outHeight = inputHeight;
        return;
    }
    
    double widthRatio = static_cast<double>(targetWidth) / inputWidth;
    double heightRatio = static_cast<double>(targetHeight) / inputHeight;
    
    double ratio = std::min(widthRatio, heightRatio);
    
    outWidth = static_cast<int>(inputWidth * ratio);
    outHeight = static_cast<int>(inputHeight * ratio);
    
    // Ensure even dimensions
    outWidth = (outWidth + 1) & ~1;
    outHeight = (outHeight + 1) & ~1;
}

bool VideoProcessor::openInputFile(const std::string& inputPath) {
    if (avformat_open_input(&inputFormatContext, inputPath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file: " << inputPath << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(inputFormatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return false;
    }
    
    // Find video and audio streams
    for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++) {
        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
            videoStreamIndex = i;
        } else if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
            audioStreamIndex = i;
        }
    }
    
    if (videoStreamIndex < 0) {
        std::cerr << "Could not find video stream" << std::endl;
        return false;
    }
    
    // Open video decoder
    const AVCodec* videoDecoder = avcodec_find_decoder(inputFormatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!videoDecoder) {
        std::cerr << "Could not find video decoder" << std::endl;
        return false;
    }
    
    inputVideoCodecContext = avcodec_alloc_context3(videoDecoder);
    if (!inputVideoCodecContext) {
        std::cerr << "Could not allocate video decoder context" << std::endl;
        return false;
    }
    
    if (avcodec_parameters_to_context(inputVideoCodecContext, 
                                    inputFormatContext->streams[videoStreamIndex]->codecpar) < 0) {
        std::cerr << "Could not copy video decoder parameters" << std::endl;
        return false;
    }
    
    if (avcodec_open2(inputVideoCodecContext, videoDecoder, nullptr) < 0) {
        std::cerr << "Could not open video decoder" << std::endl;
        return false;
    }
    
    // Open audio decoder if present
    if (audioStreamIndex >= 0) {
        const AVCodec* audioDecoder = avcodec_find_decoder(inputFormatContext->streams[audioStreamIndex]->codecpar->codec_id);
        if (!audioDecoder) {
            std::cerr << "Could not find audio decoder" << std::endl;
            return false;
        }
        
        inputAudioCodecContext = avcodec_alloc_context3(audioDecoder);
        if (!inputAudioCodecContext) {
            std::cerr << "Could not allocate audio decoder context" << std::endl;
            return false;
        }
        
        if (avcodec_parameters_to_context(inputAudioCodecContext,
                                        inputFormatContext->streams[audioStreamIndex]->codecpar) < 0) {
            std::cerr << "Could not copy audio decoder parameters" << std::endl;
            return false;
        }
        
        if (avcodec_open2(inputAudioCodecContext, audioDecoder, nullptr) < 0) {
            std::cerr << "Could not open audio decoder" << std::endl;
            return false;
        }
    }
    
    return true;
}

bool VideoProcessor::setupOutputFile(const std::string& outputPath) {
    // Create output context
    avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, outputPath.c_str());
    if (!outputFormatContext) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
    }
    
    // Setup video stream
    const AVCodec* videoEncoder = avcodec_find_encoder_by_name("libx264");
    if (!videoEncoder) {
        std::cerr << "Could not find H.264 encoder" << std::endl;
        return false;
    }
    
    AVStream* outVideoStream = avformat_new_stream(outputFormatContext, nullptr);
    if (!outVideoStream) {
        std::cerr << "Could not create output video stream" << std::endl;
        return false;
    }
    
    outputVideoCodecContext = avcodec_alloc_context3(videoEncoder);
    if (!outputVideoCodecContext) {
        std::cerr << "Could not allocate video encoder context" << std::endl;
        return false;
    }
    
    // Set video encoding parameters
    int outWidth, outHeight;
    calculateOutputDimensions(inputVideoCodecContext->width, 
                            inputVideoCodecContext->height,
                            outWidth, outHeight);
    
    outputVideoCodecContext->height = outHeight;
    outputVideoCodecContext->width = outWidth;
    outputVideoCodecContext->sample_aspect_ratio = inputVideoCodecContext->sample_aspect_ratio;
    outputVideoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    outputVideoCodecContext->time_base = inputFormatContext->streams[videoStreamIndex]->time_base;
    outputVideoCodecContext->framerate = inputFormatContext->streams[videoStreamIndex]->avg_frame_rate;
    
    // Set H.264 specific parameters
    av_opt_set(outputVideoCodecContext->priv_data, "preset", "ultrafast", 0);
    av_opt_set(outputVideoCodecContext->priv_data, "tune", "zerolatency", 0);
    outputVideoCodecContext->thread_count = THREAD_COUNT;
    
    if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        outputVideoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    // Set quality (CRF)
    outputVideoCodecContext->bit_rate = 0;  // Use CRF instead of bitrate
    av_opt_set_int(outputVideoCodecContext->priv_data, "crf", CRF, 0);
    
    if (avcodec_open2(outputVideoCodecContext, videoEncoder, nullptr) < 0) {
        std::cerr << "Could not open video encoder" << std::endl;
        return false;
    }
    
    if (avcodec_parameters_from_context(outVideoStream->codecpar, outputVideoCodecContext) < 0) {
        std::cerr << "Could not copy video encoder parameters" << std::endl;
        return false;
    }
    
    outVideoStream->time_base = outputVideoCodecContext->time_base;
    
    // Setup audio stream if present
    if (audioStreamIndex >= 0) {
        const AVCodec* audioEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioEncoder) {
            std::cerr << "Could not find AAC encoder" << std::endl;
            return false;
        }
        
        AVStream* outAudioStream = avformat_new_stream(outputFormatContext, nullptr);
        if (!outAudioStream) {
            std::cerr << "Could not create output audio stream" << std::endl;
            return false;
        }
        
        outputAudioCodecContext = avcodec_alloc_context3(audioEncoder);
        if (!outputAudioCodecContext) {
            std::cerr << "Could not allocate audio encoder context" << std::endl;
            return false;
        }
        
        // Set audio encoding parameters
        outputAudioCodecContext->sample_fmt = audioEncoder->sample_fmts[0];
        outputAudioCodecContext->bit_rate = AUDIO_BITRATE;
        outputAudioCodecContext->sample_rate = inputAudioCodecContext->sample_rate;
        
        // Update channel layout handling for newer FFmpeg
        AVChannelLayout out_ch_layout;
        av_channel_layout_copy(&out_ch_layout, &inputAudioCodecContext->ch_layout);
        av_channel_layout_copy(&outputAudioCodecContext->ch_layout, &out_ch_layout);
        
        outputAudioCodecContext->time_base = (AVRational){1, outputAudioCodecContext->sample_rate};
        
        if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            outputAudioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        if (avcodec_open2(outputAudioCodecContext, audioEncoder, nullptr) < 0) {
            std::cerr << "Could not open audio encoder" << std::endl;
            return false;
        }
        
        if (avcodec_parameters_from_context(outAudioStream->codecpar, outputAudioCodecContext) < 0) {
            std::cerr << "Could not copy audio encoder parameters" << std::endl;
            return false;
        }
        
        outAudioStream->time_base = outputAudioCodecContext->time_base;
    }
    
    // Open output file
    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFormatContext->pb, outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            return false;
        }
    }
    
    // Write header
    if (avformat_write_header(outputFormatContext, nullptr) < 0) {
        std::cerr << "Could not write output header" << std::endl;
        return false;
    }
    
    return true;
}

bool VideoProcessor::processFrames() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* swsFrame = nullptr;
    
    if (!packet || !frame) {
        std::cerr << "Could not allocate packet/frame" << std::endl;
        return false;
    }
    
    // Initialize scaling context if needed
    if (outputVideoCodecContext->width != inputVideoCodecContext->width ||
        outputVideoCodecContext->height != inputVideoCodecContext->height) {
        swsFrame = av_frame_alloc();
        if (!swsFrame) {
            std::cerr << "Could not allocate scaling frame" << std::endl;
            return false;
        }
        
        swsFrame->format = outputVideoCodecContext->pix_fmt;
        swsFrame->width = outputVideoCodecContext->width;
        swsFrame->height = outputVideoCodecContext->height;
        
        if (av_frame_get_buffer(swsFrame, 0) < 0) {
            std::cerr << "Could not allocate scaling frame buffer" << std::endl;
            return false;
        }
        
        swsContext = sws_getContext(inputVideoCodecContext->width,
                                  inputVideoCodecContext->height,
                                  inputVideoCodecContext->pix_fmt,
                                  outputVideoCodecContext->width,
                                  outputVideoCodecContext->height,
                                  outputVideoCodecContext->pix_fmt,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!swsContext) {
            std::cerr << "Could not initialize scaling context" << std::endl;
            return false;
        }
    }
    
    while (av_read_frame(inputFormatContext, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            // Decode video
            int ret = avcodec_send_packet(inputVideoCodecContext, packet);
            if (ret < 0) {
                std::cerr << "Error sending packet for decoding" << std::endl;
                return false;
            }
            
            while (ret >= 0) {
                ret = avcodec_receive_frame(inputVideoCodecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error receiving frame" << std::endl;
                    return false;
                }
                
                // Scale if needed
                AVFrame* encodeFrame = frame;
                if (swsContext) {
                    sws_scale(swsContext, frame->data, frame->linesize, 0,
                             frame->height, swsFrame->data, swsFrame->linesize);
                    swsFrame->pts = frame->pts;
                    encodeFrame = swsFrame;
                }
                
                // Encode video
                ret = avcodec_send_frame(outputVideoCodecContext, encodeFrame);
                if (ret < 0) {
                    std::cerr << "Error sending frame for encoding" << std::endl;
                    return false;
                }
                
                while (ret >= 0) {
                    AVPacket* outPacket = av_packet_alloc();
                    ret = avcodec_receive_packet(outputVideoCodecContext, outPacket);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_packet_free(&outPacket);
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error receiving packet from encoder" << std::endl;
                        av_packet_free(&outPacket);
                        return false;
                    }
                    
                    outPacket->stream_index = 0;
                    av_packet_rescale_ts(outPacket,
                                       outputVideoCodecContext->time_base,
                                       outputFormatContext->streams[0]->time_base);
                    
                    ret = av_interleaved_write_frame(outputFormatContext, outPacket);
                    av_packet_free(&outPacket);
                    if (ret < 0) {
                        std::cerr << "Error writing frame" << std::endl;
                        return false;
                    }
                }
            }
        } else if (packet->stream_index == audioStreamIndex && audioStreamIndex >= 0) {
            // Process audio similarly (simplified for brevity)
            AVPacket* outPacket = av_packet_alloc();
            av_packet_copy_props(outPacket, packet);
            outPacket->stream_index = 1;
            
            av_packet_rescale_ts(outPacket,
                               inputFormatContext->streams[audioStreamIndex]->time_base,
                               outputFormatContext->streams[1]->time_base);
            
            if (av_interleaved_write_frame(outputFormatContext, outPacket) < 0) {
                std::cerr << "Error writing audio frame" << std::endl;
                av_packet_free(&outPacket);
                return false;
            }
            av_packet_free(&outPacket);
        }
        
        av_packet_unref(packet);
    }
    
    // Flush encoders
    avcodec_send_frame(outputVideoCodecContext, nullptr);
    while (true) {
        AVPacket* outPacket = av_packet_alloc();
        int ret = avcodec_receive_packet(outputVideoCodecContext, outPacket);
        if (ret == AVERROR_EOF) {
            av_packet_free(&outPacket);
            break;
        }
        if (ret < 0) {
            std::cerr << "Error flushing video encoder" << std::endl;
            av_packet_free(&outPacket);
            return false;
        }
        
        outPacket->stream_index = 0;
        av_packet_rescale_ts(outPacket,
                           outputVideoCodecContext->time_base,
                           outputFormatContext->streams[0]->time_base);
        
        ret = av_interleaved_write_frame(outputFormatContext, outPacket);
        av_packet_free(&outPacket);
        if (ret < 0) {
            std::cerr << "Error writing frame during flush" << std::endl;
            return false;
        }
    }
    
    // Write trailer
    if (av_write_trailer(outputFormatContext) < 0) {
        std::cerr << "Error writing trailer" << std::endl;
        return false;
    }
    
    av_frame_free(&frame);
    if (swsFrame) {
        av_frame_free(&swsFrame);
    }
    av_packet_free(&packet);
    
    return true;
}

bool VideoProcessor::processVideo(const std::string& inputPath, const std::string& outputPath) {
    try {
        if (!openInputFile(inputPath)) {
            return false;
        }
        
        if (!setupOutputFile(outputPath)) {
            return false;
        }
        
        if (!processFrames()) {
            return false;
        }
        
        cleanup();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error processing video: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

void VideoProcessor::setTargetResolution(int width, int height) {
    targetWidth = width;
    targetHeight = height;
} 