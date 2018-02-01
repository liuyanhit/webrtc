#include "input.hpp"
#include "signaling.hpp"

using namespace muxer;

//
// AvReceiver
//

AvReceiver::AvReceiver()
{
}

AvReceiver::~AvReceiver()
{
        avformat_close_input(&pAvContext_);
        pAvContext_ = nullptr;
}

int AvReceiver::AvInterruptCallback(void* _pContext)
{
        using namespace std::chrono;
        AvReceiver* pReceiver = reinterpret_cast<AvReceiver*>(_pContext);
        high_resolution_clock::time_point now = high_resolution_clock::now();
        if (duration_cast<milliseconds>(now - pReceiver->start_).count() > pReceiver->nTimeout_) {
                Error("receiver timeout, %lu milliseconds", pReceiver->nTimeout_);
                return -1;
        }

        return 0;
}

int AvReceiver::Receive(IN const std::string& _url, IN PacketHandlerType& _callback)
{
        if (pAvContext_ != nullptr) {
                Warn("internal: reuse of Receiver is not recommended");
        }

        // allocate AV context
        pAvContext_ = avformat_alloc_context();
        if (pAvContext_ == nullptr) {
                Error("av context could not be created");
                return -1;
        }

        // for timeout timer
        std::string option;
        nTimeout_ = 10 * 1000; // 10 seconds
        Info("receiver timeout=%lu milliseconds", nTimeout_);
        pAvContext_->interrupt_callback.callback = AvReceiver::AvInterruptCallback;
        pAvContext_->interrupt_callback.opaque = this;
        start_ = std::chrono::high_resolution_clock::now();

        // open input stream
        Info("input URL: %s", _url.c_str());
        int nStatus = avformat_open_input(&pAvContext_, _url.c_str(), 0, 0);
        if (nStatus < 0) {
                Error("could not open input stream: %s", _url.c_str());
                return -1;
        }

        // get stream info
        nStatus = avformat_find_stream_info(pAvContext_, 0);
        if (nStatus < 0) {
                Error("could not get stream info");
                return -1;
        }
        std::vector<struct AVStream *> streams;
        for (unsigned int i = 0; i < pAvContext_->nb_streams; i++) {
                struct AVStream * pAvStream = pAvContext_->streams[i];
                streams.push_back(pAvStream);
                Info("stream is found: avstream=%d, avcodec=%d",
                     pAvStream->codecpar->codec_type, pAvStream->codecpar->codec_id);
        }

        while (true) {
                AVPacket* pAvPacket = av_packet_alloc();
                av_init_packet(pAvPacket);
                if (av_read_frame(pAvContext_, pAvPacket) == 0) {
                        if (pAvPacket->stream_index < 0 ||
                            static_cast<unsigned int>(pAvPacket->stream_index) >= pAvContext_->nb_streams) {
                                Warn("invalid stream index in packet");
                                av_packet_free(&pAvPacket);
                                continue;
                        }

                        // if avformat detects another stream during transport, we have to ignore the packets of the stream
                        if (static_cast<size_t>(pAvPacket->stream_index) < streams.size()) {
                                // we need all PTS/DTS use milliseconds, sometimes they are macroseconds such as TS streams
                                AVRational tb = AVRational{1, 1000};
                                AVRounding r = static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
                                pAvPacket->dts = av_rescale_q_rnd(pAvPacket->dts, streams[pAvPacket->stream_index]->time_base, tb, r);
                                pAvPacket->pts = av_rescale_q_rnd(pAvPacket->pts, streams[pAvPacket->stream_index]->time_base, tb, r);

                                int nStatus = _callback(std::make_unique<MediaPacket>(*streams[pAvPacket->stream_index], pAvPacket));
                                if (nStatus != 0) {
                                        return nStatus;
                                }
                        }
                        start_ = std::chrono::high_resolution_clock::now();
                } else {
                        break;
                }
        }

        return 0;
}

//
// AvDecoder
//

AvDecoder::AvDecoder()
{
}

AvDecoder::~AvDecoder()
{
        if (bIsDecoderAvailable_) {
                avcodec_close(pAvDecoderContext_);
        }
        if (pAvDecoderContext_ != nullptr) {
                avcodec_free_context(&pAvDecoderContext_);
        }
}

int AvDecoder::Init(IN const std::unique_ptr<MediaPacket>& _pPacket)
{
        // create decoder
        if (pAvDecoderContext_ == nullptr) {
                // find decoder
                AVCodec *pAvCodec = avcodec_find_decoder(static_cast<AVCodecID>(_pPacket->Codec()));
                if (pAvCodec == nullptr) {
                        Error("could not find AV decoder for codec_id=%d", _pPacket->Codec());
                        return -1;
                }

                // initiate AVCodecContext
                pAvDecoderContext_ = avcodec_alloc_context3(pAvCodec);
                if (pAvDecoderContext_ == nullptr) {
                        Error("could not allocate AV codec context");
                        return -1;
                }

                // if the packet is from libavformat
                // just use context parameters in AVStream to get one directly otherwise fake one
                if (_pPacket->AvCodecParameters() != nullptr) {
                        if (avcodec_parameters_to_context(pAvDecoderContext_, _pPacket->AvCodecParameters()) < 0){
                                Error("could not copy decoder context");
                                return -1;
                        }
                }

                // open it
                if (avcodec_open2(pAvDecoderContext_, pAvCodec, nullptr) < 0) {
                        Error("could not open decoder");
                        return -1;
                } else {
                        Info("open decoder: stream=%d, codec=%d", _pPacket->Stream(), _pPacket->Codec());
                        bIsDecoderAvailable_ = true;
                }
        }

        return 0;
}

int AvDecoder::Decode(IN const std::unique_ptr<MediaPacket>& _pPacket, IN FrameHandlerType& _callback)
{
        if (Init(_pPacket) < 0) {
                return -1;
        }

       // int nStatus;

        //
        // decode ! and get one frame to encode
        //
        do {
                bool bNeedSendAgain = false;
                int nStatus = avcodec_send_packet(pAvDecoderContext_, _pPacket->AvPacket());
                if (nStatus != 0) {
                        if (nStatus == AVERROR(EAGAIN)) {
                                Warn("decoder internal: assert failed, we should not get EAGAIN");
                                bNeedSendAgain = true;
                        } else {
                                Error("decoder: could not send frame, status=%d", nStatus);
                                _pPacket->Print();
                                return -1;
                        }
                }

                while (1) {
                        // allocate a frame for outputs
                        auto pFrame = std::make_shared<MediaFrame>();
                        pFrame->Stream(_pPacket->Stream());
                        pFrame->Codec(_pPacket->Codec());

                        nStatus = avcodec_receive_frame(pAvDecoderContext_, pFrame->AvFrame());
                        if (nStatus == 0) {
                                int nStatus = _callback(pFrame);
                                if (nStatus < 0) {
                                        return nStatus;
                                }
                                if (bNeedSendAgain) {
                                        break;
                                }
                        } else if (nStatus == AVERROR(EAGAIN)) {
                                return 0;
                        } else {
                                Error("decoder: could not receive frame, status=%d", nStatus);
                                _pPacket->Print();
                                return -1;
                        }
                }
        } while(1);

        return 0;
}

//
// Input
//

Input::Input(IN const std::string& _name)
        :OptionMap(),
         name_(_name),
         videoQ_(Input::VIDEO_Q_LEN),
         audioQ_(Input::AUDIO_Q_LEN)
{
        bReceiverExit_.store(false);

        // reserve sample buffer pool for format S16
        sampleBuffer_.reserve(AudioResampler::FRAME_SIZE * AudioResampler::CHANNELS * 2 * 4);
        sampleBuffer_.resize(0);
}

void Input::StartRtc(IN const Json::Value& m) {
        auto c = gS->NewRtcConn(m);
        isrtc_ = true;
        rtcconn_ = c;

        c->OnVideo = [this](const webrtc::VideoFrame& rtcframe) {
                if (rtcframe.height() == 0 || rtcframe.width() == 0) {
                        Warn("RtcVideoFrame invalid size %dx%d", rtcframe.height(), rtcframe.width());
                        return;
                }

                std::shared_ptr<MediaFrame> frame = std::make_shared<MediaFrame>();
                frame->Stream(STREAM_VIDEO);
                frame->Codec(CODEC_H264);
                frame->AvFrame()->format = AV_PIX_FMT_YUV420P;
                frame->AvFrame()->height = rtcframe.height();
                frame->AvFrame()->width = rtcframe.width();
                av_frame_get_buffer(frame->AvFrame(), 32);

                auto rtcfb = rtcframe.video_frame_buffer();
                auto i420 = rtcfb->ToI420();

                if (0) {
                        LOG(INFO) << "OnVideo " << int(rtcfb->type()) <<
                                " " << i420->StrideY() <<
                                " " << i420->StrideU() <<
                                " " << i420->StrideV() <<
                                " " << frame->AvFrame()->linesize[0] <<
                                " " << frame->AvFrame()->linesize[1] <<
                                " " << frame->AvFrame()->linesize[2] <<
                                ""
                                ;
                }

                int rtclinesize[3] = {
                        i420->StrideY(),
                        i420->StrideU(),
                        i420->StrideV(),
                };
                int rtcdatasize[3] = {
                        i420->StrideY()*rtcframe.height(),
                        i420->StrideU()*i420->ChromaHeight(),
                        i420->StrideV()*i420->ChromaHeight(),
                };
                const uint8_t* rtcdata[3] = {
                        i420->DataY(),
                        i420->DataU(),
                        i420->DataV(),
                };

                for (int i = 0; i < 3; i++) {
                        if (rtcdata[i] == NULL) {
                                Warn("RtcVideoBuffer invalid data[%d]==NULL", i);
                                return;
                        }
                        if (rtclinesize[i] > frame->AvFrame()->linesize[i]) {
                                Warn("RtcVideoBuffer invalid linesize[%d]=%d > %d", i, rtclinesize[i], frame->AvFrame()->linesize[i]);
                                return;
                        }
                        memcpy(frame->AvFrame()->data[i], rtcdata[i], rtcdatasize[i]);
                }

                /*
                static int rtcdumpfilenr = 0;
                char filename[512];
                sprintf(filename, "/tmp/%d.yuv", rtcdumpfilenr++);
                FILE *fp = fopen(filename, "wb+");
                fwrite(i420->DataY(), 1, i420->StrideY()*rtcframe.height(), fp);
                fwrite(i420->DataU(), 1, i420->StrideU()*i420->ChromaHeight(), fp);
                fwrite(i420->DataV(), 1, i420->StrideV()*i420->ChromaHeight(), fp);
                fclose(fp);
                */

                SetVideo(frame);
        };

        c->OnAudio = [this](const void* audio_data,
                int bits_per_sample,
                int sample_rate,
                size_t number_of_channels,
                size_t number_of_frames)
        {
                auto frame = std::make_shared<MediaFrame>();
                frame->Stream(STREAM_AUDIO);
                frame->Codec(CODEC_AAC);
                frame->AvFrame()->format = AV_SAMPLE_FMT_S16;
                frame->AvFrame()->channel_layout = AV_CH_LAYOUT_MONO;
                frame->AvFrame()->sample_rate = sample_rate;
                frame->AvFrame()->channels = number_of_channels;
                frame->AvFrame()->nb_samples = number_of_frames;
                av_frame_get_buffer(frame->AvFrame(), 0);

                memcpy(frame->AvFrame()->data[0], audio_data, bits_per_sample/8*number_of_frames);
                SetAudio(frame);

                DebugPCM("/tmp/rtc.orig.s16", audio_data, bits_per_sample/8*number_of_frames);
        };

        c->Start();
        rtcconn_ = c;
}

// start thread => receiver loop => decoder loop
void Input::Start(IN const std::string& _url)
{
        auto recv = [this, _url] {
                while (bReceiverExit_.load() == false) {
                        auto avReceiver = std::make_unique<AvReceiver>();
                        auto vDecoder = std::make_unique<AvDecoder>();
                        auto aDecoder = std::make_unique<AvDecoder>();

                        auto receiverHook = [&](IN const std::unique_ptr<MediaPacket> _pPacket) -> int {
                                if (bReceiverExit_.load() == true) {
                                        return -1;
                                }

                                auto decoderHook = [&](const std::shared_ptr<MediaFrame>& _pFrame) -> int {
                                        if (bReceiverExit_.load() == true) {
                                                return -1;
                                        }

                                        // format video/audio data
                                        if (_pFrame->Stream() == STREAM_VIDEO) {
                                                SetVideo(_pFrame);
                                        } else if (_pFrame->Stream() == STREAM_AUDIO) {
                                                SetAudio(_pFrame);
                                        }

                                        return 0;
                                };

                                // start decoder loop
                                if (_pPacket->Stream() == STREAM_VIDEO) {
                                        vDecoder->Decode(_pPacket, decoderHook);
                                } else if (_pPacket->Stream() == STREAM_AUDIO) {
                                        aDecoder->Decode(_pPacket, decoderHook);
                                }

                                return 0;
                        };

                        // start receiver loop
                        avReceiver->Receive(_url, receiverHook);
                }
        };

        receiver_ = std::thread(recv);
}

void Input::Stop()
{
        if (isrtc_) {
                return;
        }
        
        bReceiverExit_.store(true);
        if (receiver_.joinable()) {
                receiver_.join();
        }
}

void Input::SetVideo(const std::shared_ptr<MediaFrame>& _pFrame)
{
        // convert color space to YUV420p and rescale the image

        int nW, nH;
        bool bNeedRescale = false;
        if (pRescaler_ == nullptr) {
                auto pAvf = _pFrame->AvFrame();
                if (GetOption(options::width, nW) == true && nW != pAvf->width) {
                        bNeedRescale = true;
                }
                if (GetOption(options::height, nH) == true && nH != pAvf->height) {
                        bNeedRescale = true;
                }
                if (pAvf->format != VideoRescaler::PIXEL_FMT) {
                        bNeedRescale = true;
                }
                if (bNeedRescale) {
                        pRescaler_ = std::make_shared<VideoRescaler>(nW, nH);
                }
        } else {
                // if target w or h is changed, reinit the rescaler
                if (GetOption(options::width, nW) == true && nW != pRescaler_->TargetW()) {
                        bNeedRescale = true;
                }
                if (GetOption(options::height, nH) == true && nH != pRescaler_->TargetH()) {
                        bNeedRescale = true;
                }
                if (bNeedRescale) {
                        pRescaler_->Reset(nW, nH);
                }
        }


        // rescale the video frame
        auto pFrame = _pFrame;
        if (pRescaler_ != nullptr) {
                pRescaler_->Rescale(_pFrame, pFrame);
        }

        // set x,y,z coordinate
        int nX, nY, nZ;
        if (GetOption(options::x, nX) == true) {
                pFrame->X(nX);
        }
        if (GetOption(options::y, nY) == true) {
                pFrame->Y(nY);
        }
        if (GetOption(options::z, nZ) == true) {
                pFrame->Z(nZ);
        }

        videoQ_.ForcePush(pFrame);
}

void Input::SetAudio(const std::shared_ptr<MediaFrame>& _pFrame)
{
        // resample the audio and push data to the buffer

        // lock buffer and queue
        std::lock_guard<std::mutex> lock(sampleBufferLck_);

        //size_t nLen;
        size_t nBufSize = sampleBuffer_.size();
        std::vector<uint8_t> buffer;

        // resample to the same audio format
        if (resampler_.Resample(_pFrame, buffer) != 0) {
                return;
        }

        // save resampled data in audio buffer
        sampleBuffer_.resize(sampleBuffer_.size() + buffer.size());
        std::copy(buffer.begin(), buffer.end(), &sampleBuffer_[nBufSize]);

        // if the buffer size meets the min requirement of encoding one frame, build a frame and push upon audio queue
        size_t nSizeEachFrame = AudioResampler::FRAME_SIZE * AudioResampler::CHANNELS * av_get_bytes_per_sample(AudioResampler::SAMPLE_FMT);

        //Info("resample %lu %lu %lu %p", buffer.size(), sampleBuffer_.size(), nSizeEachFrame, &sampleBuffer_);

        while (sampleBuffer_.size() >= nSizeEachFrame) {
                auto pNewFrame = std::make_shared<MediaFrame>();
                pNewFrame->Stream(_pFrame->Stream());
                pNewFrame->Codec(_pFrame->Codec());
                av_frame_copy_props(pNewFrame->AvFrame(), _pFrame->AvFrame());
                pNewFrame->AvFrame()->nb_samples = AudioResampler::FRAME_SIZE;
                pNewFrame->AvFrame()->format = AudioResampler::SAMPLE_FMT;
                pNewFrame->AvFrame()->channels = AudioResampler::CHANNELS;
                pNewFrame->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
                pNewFrame->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
                av_frame_get_buffer(pNewFrame->AvFrame(), 0);
                std::copy(&sampleBuffer_[0], &sampleBuffer_[nSizeEachFrame], pNewFrame->AvFrame()->data[0]);
        
                DebugPCM("/tmp/rtc.re2.s16", &sampleBuffer_[0], nSizeEachFrame);

                // move rest samples to beginning of the buffer
                std::copy(&sampleBuffer_[nSizeEachFrame], &sampleBuffer_[sampleBuffer_.size()], sampleBuffer_.begin());
                sampleBuffer_.resize(sampleBuffer_.size() - nSizeEachFrame);

                audioQ_.ForcePush(pNewFrame);
        }
}

bool Input::GetVideo(OUT std::shared_ptr<MediaFrame>& _pFrame, OUT size_t& _nQlen)
{
        // make sure preserve at least one video frame
        bool bOk = videoQ_.TryPop(pLastVideo_);
        _nQlen = videoQ_.Size();
        if (bOk == true){
                _pFrame = pLastVideo_;
        } else {
                if (pLastVideo_ == nullptr) {
                        return false;
                }
                _pFrame = pLastVideo_;
        }
        return true;
}

bool Input::GetAudio(OUT std::shared_ptr<MediaFrame>& _pFrame, OUT size_t& _nQlen)
{
        // lock buffer and queue
        std::lock_guard<std::mutex> lock(sampleBufferLck_);
        //size_t nSizeEachFrame = AudioResampler::FRAME_SIZE * AudioResampler::CHANNELS * av_get_bytes_per_sample(AudioResampler::SAMPLE_FMT);

        bool bOk = audioQ_.TryPop(_pFrame);

        /*
        if (bOk) {
                Info("getaudio pop");
        }
        */

        //_nQlen = audioQ_.Size();
        /*
        if (bOk == true) {
                auto nLen = audioQ_.Size();
                if (nLen == Input::AUDIO_Q_LEN - 1) {
                        Warn("[%s] input audio queue is full", Name().c_str());
                } else if (nLen == Input::AUDIO_Q_LEN - 5) {
                        Warn("[%s] input audio queue is almost full", Name().c_str());
                } else if (nLen == 5) {
                        Warn("[%s] input audio queue is almost empty", Name().c_str());
                } else if (nLen == 1) {
                        Warn("[%s] input audio queue is empty", Name().c_str());
                }
                return true;
        } else {
                Info("samplebuffer slience %lu", sampleBuffer_.size());
                if (sampleBuffer_.size() > 0 && sampleBuffer_.size() < nSizeEachFrame) {
                        // sample buffer does contain data but less than frame size
                        _pFrame = std::make_shared<MediaFrame>();
                        _pFrame->Stream(STREAM_AUDIO);
                        _pFrame->Codec(CODEC_AAC);
                        _pFrame->AvFrame()->nb_samples = AudioResampler::FRAME_SIZE;
                        _pFrame->AvFrame()->format = AudioResampler::SAMPLE_FMT;
                        _pFrame->AvFrame()->channels = AudioResampler::CHANNELS;
                        _pFrame->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
                        _pFrame->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
                        av_frame_get_buffer(_pFrame->AvFrame(), 0);
                        av_samples_set_silence(_pFrame->AvFrame()->data, 0, _pFrame->AvFrame()->nb_samples, _pFrame->AvFrame()->channels,
                                               (AVSampleFormat)_pFrame->AvFrame()->format);

                        // copy existing buffer contents
                        std::copy(&sampleBuffer_[0], &sampleBuffer_[sampleBuffer_.size()], _pFrame->AvFrame()->data[0]);
                        sampleBuffer_.resize(0);

                        return true;
                }
        }
        */

        return bOk;
}

Input::~Input()
{
        Stop();
}

std::string Input::Name()
{
        return name_;
}
