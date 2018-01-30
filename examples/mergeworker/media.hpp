#ifndef __MEDIA_HPP__
#define __MEDIA_HPP__

#include "common.hpp"

namespace muxer
{
        class MediaFrame;
        class AudioResampler
        {
        public:
                static const int CHANNELS = 2;
                static const AVSampleFormat SAMPLE_FMT = AV_SAMPLE_FMT_S16;
                static const int CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;
                static const int FRAME_SIZE = 1024;
                static const int SAMPLE_RATE = 44100;
        public:
                AudioResampler();
                ~AudioResampler();
                int Resample(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::vector<uint8_t>& buffer);
        private:
                int InitAudioResampling(IN const std::shared_ptr<MediaFrame>& pFrame);
        private:
                SwrContext* pSwr_ = nullptr; // for resampling
        };

        class VideoRescaler
        {
        public:
                static const AVPixelFormat PIXEL_FMT = AV_PIX_FMT_YUV420P;
        public:
                VideoRescaler(IN int nWidth, IN int nHeight);
                ~VideoRescaler();
                int Rescale(IN const std::shared_ptr<MediaFrame>& pInFrame, OUT std::shared_ptr<MediaFrame>& pOutFrame);
                int Reset(IN int nWidth, IN int nHeight);
                int TargetW();
                int TargetH();
        private:
                int Init(IN const std::shared_ptr<MediaFrame>& pFrame);
        private:
                SwsContext* pSws_ = nullptr;
                int nW_, nH_, nOrigW_, nOrigH_;
        };
}

#endif
