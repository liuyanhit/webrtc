#include "media.hpp"

using namespace muxer;

//
// AudioResampler
//

AudioResampler::AudioResampler()
{
}

AudioResampler::~AudioResampler()
{
        if (pSwr_ != nullptr) {
                swr_free(&pSwr_);
        }
}

int AudioResampler::InitAudioResampling(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        // for fdkaac encoder, input samples should be PCM signed16le, otherwise do resampling
        Info("resampling input: format=%d, layout=%d rate=%d output: format=%d layout=%d rate=%d",
                _pFrame->AvFrame()->format, int(av_get_default_channel_layout(_pFrame->AvFrame()->channels)),
                _pFrame->AvFrame()->sample_rate,
                AudioResampler::SAMPLE_FMT, AudioResampler::CHANNEL_LAYOUT,
                AudioResampler::SAMPLE_RATE);
        pSwr_ = swr_alloc();
        av_opt_set_int(pSwr_, "in_channel_layout", av_get_default_channel_layout(_pFrame->AvFrame()->channels), 0);
        av_opt_set_int(pSwr_, "out_channel_layout", AudioResampler::CHANNEL_LAYOUT, 0);
        av_opt_set_int(pSwr_, "in_sample_rate", _pFrame->AvFrame()->sample_rate, 0);
        av_opt_set_int(pSwr_, "out_sample_rate", AudioResampler::SAMPLE_RATE, 0);
        av_opt_set_sample_fmt(pSwr_, "in_sample_fmt", static_cast<AVSampleFormat>(_pFrame->AvFrame()->format), 0);
        av_opt_set_sample_fmt(pSwr_, "out_sample_fmt", AudioResampler::SAMPLE_FMT,  0);
        if (swr_init(pSwr_) != 0) {
                Error("could not initiate resampling");
                return -1;
        }

        return 0;
}

int AudioResampler::Resample(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::vector<uint8_t>& _buffer)
{
        if (pSwr_ == nullptr) {
                if (InitAudioResampling(_pInFrame) != 0) {
                        Error("could not init resampling");
                        return -1;
                }
        }

        int nRetVal;
        uint8_t **pDstData = nullptr;
        int nDstLinesize;
        int nDstBufSize;
        int nDstNbSamples = av_rescale_rnd(_pInFrame->AvFrame()->nb_samples, AudioResampler::SAMPLE_RATE,
                                            _pInFrame->AvFrame()->sample_rate, AV_ROUND_UP);
        int nMaxDstNbSamples = nDstNbSamples;

        // get output buffer
        nRetVal = av_samples_alloc_array_and_samples(&pDstData, &nDstLinesize, AudioResampler::CHANNELS,
                                                 nDstNbSamples, AudioResampler::SAMPLE_FMT, 0);
        if (nRetVal < 0) {
                Error("resampler: could not allocate destination samples");
                return -1;
        }

        // get output samples
        nDstNbSamples = av_rescale_rnd(swr_get_delay(pSwr_, _pInFrame->AvFrame()->sample_rate) + _pInFrame->AvFrame()->nb_samples,
                                       AudioResampler::SAMPLE_RATE, _pInFrame->AvFrame()->sample_rate, AV_ROUND_UP);
        if (nDstNbSamples > nMaxDstNbSamples) {
                av_freep(&pDstData[0]);
                nRetVal = av_samples_alloc(pDstData, &nDstLinesize, AudioResampler::CHANNELS,
                                           nDstNbSamples, AudioResampler::SAMPLE_FMT, 1);
                if (nRetVal < 0) {
                        Error("resampler: could not allocate sample buffer");
                        return -1;
                }
                nMaxDstNbSamples = nDstNbSamples;
        }

        // convert !!
        nRetVal = swr_convert(pSwr_, pDstData, nDstNbSamples, (const uint8_t **)_pInFrame->AvFrame()->extended_data,
                              _pInFrame->AvFrame()->nb_samples);
        if (nRetVal < 0) {
                Error("resampler: converting failed");
                return -1;
        }

        // get output buffer size
        nDstBufSize = av_samples_get_buffer_size(&nDstLinesize, AudioResampler::CHANNELS, nRetVal, AudioResampler::SAMPLE_FMT, 1);
        if (nDstBufSize < 0) {
                Error("resampler: could not get sample buffer size");
                return -1;
        }

        _buffer.resize(nDstBufSize);
        std::copy(pDstData[0], pDstData[0] + nDstBufSize, _buffer.begin());

        // cleanup
        if (pDstData)
                av_freep(&pDstData[0]);
        av_freep(&pDstData);

        static FILE *fp;
        if (fp == NULL) {
                fp = fopen("/tmp/rtc.s16.re1", "wb+");
        }
        fwrite(_buffer.data(), _buffer.size(), 1, fp);
        fflush(fp);

        return 0;
}

//
// VideoRescaler
//

VideoRescaler::VideoRescaler(IN int _nWidth, IN int _nHeight)
{
        if (_nWidth <= 0 || _nHeight <= 0) {
                Error("rescale: resize to width=%d, height=%d", _nWidth, _nHeight);
                return;
        }

        nW_ = _nWidth;
        nH_ = _nHeight;
}

VideoRescaler::~VideoRescaler()
{
        if (pSws_ != nullptr) {
                sws_freeContext(pSws_);
        }
}

int VideoRescaler::Reset(IN int _nWidth, IN int _nHeight) {

        if (_nWidth <= 0 || _nHeight <= 0) {
                Error("rescale: resize to width=%d, height=%d", _nWidth, _nHeight);
                return -1;
        }

        nW_ = _nWidth;
        nH_ = _nHeight;

        if (pSws_ != nullptr) {
                sws_freeContext(pSws_);
                pSws_ = nullptr;
        }

        return 0;
}

int VideoRescaler::Init(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (pSws_ != nullptr) {
                Warn("internal: rescale: already init");
                return -1;
        }

        auto pAvf = _pFrame->AvFrame();

        Info("input color_space=%d, need color_space=%d, resize_to=%dx%d, initiate rescaling",
             pAvf->format, VideoRescaler::PIXEL_FMT, nW_, nH_);
        pSws_ = sws_getContext(pAvf->width, pAvf->height,
                               static_cast<AVPixelFormat>(pAvf->format), nW_, nH_, VideoRescaler::PIXEL_FMT,
                               SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (pSws_ == nullptr) {
                Error("rescaler initialization failed");
                return -1;
        }

        nOrigW_ = pAvf->width;
        nOrigH_ = pAvf->height;

        return 0;
}

int VideoRescaler::Rescale(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::shared_ptr<MediaFrame>& _pOutFrame)
{
        if (pSws_ == nullptr) {
                if (Init(_pInFrame) != 0) {
                        pSws_ = nullptr;
                        return -1;
                }
        }

        // the incoming frame resolution changed, reinit the sws
        if (_pInFrame->AvFrame()->width != nOrigW_ || _pInFrame->AvFrame()->height != nOrigH_) {
                if (Reset(nW_, nH_) != 0) {
                        Error("rescaler: reinit failed");
                        return -2;
                }
                if (Init(_pInFrame) != 0) {
                        pSws_ = nullptr;
                        return -3;
                }
        }

        _pOutFrame = std::make_shared<MediaFrame>();
        _pOutFrame->Stream(STREAM_VIDEO);
        _pOutFrame->Codec(CODEC_H264);
        _pOutFrame->AvFrame()->format = VideoRescaler::PIXEL_FMT;
        _pOutFrame->AvFrame()->width = nW_;
        _pOutFrame->AvFrame()->height = nH_;
        av_frame_get_buffer(_pOutFrame->AvFrame(), 32);

        int nStatus = sws_scale(pSws_, _pInFrame->AvFrame()->data, _pInFrame->AvFrame()->linesize, 0,
                                _pInFrame->AvFrame()->height, _pOutFrame->AvFrame()->data, _pOutFrame->AvFrame()->linesize);
        if (nStatus < 0) {
                Error("rescale: failed, status=%d", nStatus);
                return -1;
        }

        return 0;
}

int VideoRescaler::TargetW()
{
        return nW_;
}

int VideoRescaler::TargetH()
{
        return nH_;
}
