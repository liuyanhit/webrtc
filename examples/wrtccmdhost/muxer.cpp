#include "muxer.hpp"

using namespace muxer;

//
// OptionMap
//

bool OptionMap::GetOption(IN const std::string& _key, OUT std::string& _value)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        auto it = params_.find(_key);
        if (it != params_.end()) {
                _value = it->second;
                return true;
        }
        return false;
}

bool OptionMap::GetOption(IN const std::string& _key, OUT int& _value)
{
        std::lock_guard<std::mutex> lock(intparamsLck_);

        auto it = intparams_.find(_key);
        if (it != intparams_.end()) {
                _value = it->second;
                return true;
        }
        return false;
}

bool OptionMap::GetOption(IN const std::string& _key)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        if (params_.find(_key) != params_.end()) {
                return true;
        }
        return false;
}

void OptionMap::SetOption(IN const std::string& _key, IN const std::string& _val)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        params_[_key] = _val;
}

void OptionMap::SetOption(IN const std::string& _key, IN int _val)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        intparams_[_key] = _val;
}

void OptionMap::SetOption(IN const std::string& _flag)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        SetOption(_flag, "");
}

void OptionMap::DelOption(IN const std::string& _key)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        params_.erase(_key);
}

void OptionMap::GetOptions(IN const OptionMap& _opts)
{
        params_ = _opts.params_;
        intparams_ = _opts.intparams_;
}

//
// AvMuxer
//

AvMuxer::AvMuxer(IN int _nWidth, IN int _nHeight)
        :videoMuxer_(_nWidth, _nHeight)
{
        audioOnly_.store(false);
        av_register_all();
        avformat_network_init();
}

AvMuxer::~AvMuxer()
{
}

int AvMuxer::AddOutput(IN const std::string& _name, IN const std::string& _url)
{
        auto r = std::make_shared<Output>(_name);
        r->Start(_url);
        outputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::AddOutput(IN const std::string& _name, IN FrameSender* stream)
{
        auto r = std::make_shared<Output>(_name);
        r->Start(stream);
        outputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::AddOutput(IN const std::string& _name, IN const std::string& _url, IN const Option& _opt)
{
        auto r = std::make_shared<Output>(_name);
        r->GetOptions(_opt);
        r->Start(_url);
        outputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::ModOutputOption(IN const std::string& _name, IN const std::string& _key, IN const std::string& _val)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _val);

        return 0;
}

int AvMuxer::ModOutputOption(IN const std::string& _name, IN const std::string& _key, IN int _nVal)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _nVal);

        return 0;
}

int AvMuxer::DelOutputOption(IN const std::string& _name, IN const std::string& _key)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->DelOption(_key);

        return 0;
}

int AvMuxer::RemoveOutput(IN const std::string& _key)
{
        return 0;
}

int AvMuxer::AddInput(IN const std::string& _name, IN const std::string& _url)
{
        auto r = std::make_shared<Input>(_name);
        r->Start(_url);
        inputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::AddInput(IN const std::string& _name, IN SinkAddRemover *stream)
{
        auto r = std::make_shared<Input>(_name);
        r->Start(stream);
        inputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::AddInput(IN const std::string& _name, IN const std::string& _url, IN const Option& _opt)
{
        auto r = std::make_shared<Input>(_name);
        r->GetOptions(_opt);
        r->Start(_url);
        inputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::ModInputOption(IN const std::string& _name, IN const std::string& _key, IN const std::string& _val)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _val);

        return 0;
}

int AvMuxer::ModInputOption(IN const std::string& _name, IN const std::string& _key, IN int _nVal)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _nVal);

        return 0;
}

int AvMuxer::DelInputOption(IN const std::string& _name, IN const std::string& _key)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->DelOption(_key);

        return 0;
}

int AvMuxer::RemoveInput(IN const std::string& _name)
{
        inputs_.CriticalSection([_name](std::deque<std::shared_ptr<Input>>& queue){
                        for (auto it = queue.begin(); it != queue.end(); it++) {
                                if ((*it)->Name().compare(_name) == 0) {
                                        it = queue.erase(it);
                                        return;
                                }
                        }
                });

        return 0;
}

std::shared_ptr<Input> AvMuxer::FindInput(IN const std::string& _name)
{
        std::shared_ptr<Input> p = nullptr;
        inputs_.FindIf([&](std::shared_ptr<Input>& _pInput) -> bool {
                        if (_pInput->Name().compare(_name) == 0) {
                                p = _pInput;
                                return true;
                        }
                        return false;
                });

        return p;
}

std::shared_ptr<Output> AvMuxer::FindOutput(IN const std::string& _name)
{
        std::shared_ptr<Output> p = nullptr;
        outputs_.FindIf([&](std::shared_ptr<Output>& _pOutput) -> bool {
                        if (_pOutput->Name().compare(_name) == 0) {
                                p = _pOutput;
                                return true;
                        }
                        return false;
                });

        return p;
}

int AvMuxer::Start()
{
        auto muxVideo = [this](){
                std::vector<std::shared_ptr<MediaFrame>> videoFrames;
                std::shared_ptr<MediaFrame> pOutFrame;
                size_t nQlen;

                while (true) {
                        if (audioOnly_.load()) {
                                goto sleep;
                        }
                        inputs_.Foreach([&](std::shared_ptr<Input>& _pInput){
                                        std::shared_ptr<MediaFrame> pFrame;
                                        if (_pInput->GetOption(options::hidden) == false &&
                                            _pInput->GetVideo(pFrame, nQlen) == true) {
                                                videoFrames.push_back(pFrame);
                                        }
                                });

                        if (videoFrames.size() == 0) {
                                goto sleep;
                        }

                        // background color
                        int nRGB;
                        if (GetOption(options::bgcolor, nRGB) == true) {
                                videoMuxer_.BgColor(nRGB);
                        }

                        // mux pictures
                        if (videoMuxer_.Mux(videoFrames, pOutFrame) == 0) {
                                FeedOutputs(pOutFrame);
                        }
                        videoFrames.clear();
                sleep:
                        usleep(40 * 1000);
                }
        };

        auto muxAudio = [this](){
                std::vector<std::shared_ptr<MediaFrame>> audioFrames;
                std::shared_ptr<MediaFrame> pOutFrame;
                // vars for simple congestion control

                while (true) {
                        size_t n = 0;
                        inputs_.Foreach([&](std::shared_ptr<Input>& _pInput) {
                                if (_pInput->GetOption(options::muted)) {
                                        return;
                                }
                                std::shared_ptr<MediaFrame> pFrame;
                                if (_pInput->GetAudioLatest(pFrame, 3)) {
                                        audioFrames.push_back(pFrame);
                                }
                                n++;
                        });
                        if (audioFrames.size() == 0 || audioFrames.size() < n) {
                                usleep(int(double(AudioResampler::DEFAULT_FRAME_SIZE)/AudioResampler::SAMPLE_RATE*1000));
                                continue;
                        }
                        //DebugPCM("/tmp/rtc.mix0.s16", audioFrames[0]->AvFrame()->data[0], audioFrames[0]->AvFrame()->linesize[0]);

                        if (audioMixer_.Mix(audioFrames, pOutFrame) == 0) {
                                DebugPCM("/tmp/rtc.mix.s16", pOutFrame->AvFrame()->data[0], pOutFrame->AvFrame()->linesize[0]);
                                FeedOutputs(pOutFrame);
                        }
                        audioFrames.clear();
                }
        };

        videoMuxerThread_ = std::thread(muxVideo);
        audioMuxerThread_ = std::thread(muxAudio);

        return 0;
}

void AvMuxer::FeedOutputs(IN std::shared_ptr<MediaFrame>& _pFrame)
{
        uint64_t nMilliseconds = 0;

        // cretical area for the internal clock
        {
                struct timeval  tv;
                struct timezone tz;
                std::lock_guard<std::mutex> lock(clockLck_);
                gettimeofday(&tv, &tz);
                nMilliseconds = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
                if (nInitClock_ == 0) {
                        nInitClock_ = nMilliseconds;
                }
        }
        _pFrame->AvFrame()->pts = nMilliseconds - nInitClock_;

        outputs_.Foreach([&](std::shared_ptr<Output>& _pOutput) {
                        bool ok = _pOutput->Push(_pFrame);
                        if (!ok) {
                                Verbose("FeedOutputs PushFailed isaudio %d", _pFrame->Stream() == StreamType::STREAM_AUDIO);
                        }
                });
}

//
// VideoMuxer
//

VideoMuxer::VideoMuxer(IN int _nW, IN int _nH)
{
        nCanvasW_ = _nW;
        nCanvasH_ = _nH;
}

VideoMuxer::~VideoMuxer()
{
}

void VideoMuxer::BgColor(int _nRGB)
{
        nBackground_ = _nRGB;
}

int VideoMuxer::Mux(IN std::vector<std::shared_ptr<MediaFrame>>& _frames, OUT std::shared_ptr<MediaFrame>& _pOut)
{
        auto pMuxed = std::make_shared<MediaFrame>();
        pMuxed->Stream(STREAM_VIDEO);
        pMuxed->Codec(CODEC_H264);
        pMuxed->AvFrame()->format = VideoRescaler::PIXEL_FMT;
        pMuxed->AvFrame()->width = nCanvasW_;
        pMuxed->AvFrame()->height = nCanvasH_;
        av_frame_get_buffer(pMuxed->AvFrame(), 32);

        // by default the canvas is pure black, or customized background color
        uint8_t nR = nBackground_ >> 16;
        uint8_t nG = (nBackground_ >> 8) & 0xff;
        uint8_t nB = nBackground_ & 0xff;
        uint8_t nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
        uint8_t nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
        uint8_t nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);

        memset(pMuxed->AvFrame()->data[0], nY, pMuxed->AvFrame()->linesize[0] * nCanvasH_);
        memset(pMuxed->AvFrame()->data[1], nU, pMuxed->AvFrame()->linesize[1] * (nCanvasH_ / 2));
        memset(pMuxed->AvFrame()->data[2], nV, pMuxed->AvFrame()->linesize[2] * (nCanvasH_ / 2));

        // sort by Z coordinate
        std::sort(_frames.begin(), _frames.end(),
                  [](const std::shared_ptr<MediaFrame>& i, const std::shared_ptr<MediaFrame>& j) {
                          return i->Z() < j->Z();
                });

        // mux pictures
        for (auto& pFrame : _frames) {
                if (pFrame == nullptr) {
                        Warn("internal: got 1 null frame, something was wrong");
                        continue;
                }
                Overlay(pFrame, pMuxed);
        }

        _pOut = pMuxed;

        return 0;
}

void VideoMuxer::Overlay(IN const std::shared_ptr<MediaFrame>& _pFrom, OUT std::shared_ptr<MediaFrame>& _pTo)
{
        AVFrame* pFrom = _pFrom->AvFrame();
        AVFrame* pTo = _pTo->AvFrame();
        auto nX = _pFrom->X();
        auto nY = _pFrom->Y();

        if (pFrom == nullptr || pTo == nullptr) {
                return;
        }

        // x or y is beyond width and height of target frame
        if (nX >= pTo->width || nY >= pTo->height) {
                return;
        }

        // Y plane

        // left-top offset point of source frame from which source frame will be copied
        int32_t nFromOffset     = 0;
        int32_t nFromOffsetX    = 0;
        int32_t nFromOffsetY    = 0;

        // left-top offset point of target frame to which source frame will copy data
        int32_t nToOffset       = 0;
        int32_t nToOffsetX      = 0;
        int32_t nToOffsetY      = 0;

        // final resolution of the source frame
        int32_t nTargetH        = 0;
        int32_t nTargetW        = 0;

        if (nX < 0) {
                if (nX + pFrom->width < 0) {
                        // whole frame is to the left side of target
                        return;
                }
                nFromOffsetX = -nX;
                nToOffsetX = 0;
                nTargetW = (pFrom->width + nX < pTo->width) ? (pFrom->width + nX) : pTo->width;
        } else {
                nFromOffsetX = 0;
                nToOffsetX = nX;
                nTargetW = (nToOffsetX + pFrom->width > pTo->width) ? (pTo->width - nToOffsetX) : pFrom->width;
        }

        if (nY < 0) {
                if (nY + pFrom->height < 0) {
                        // whole original frame is beyond top side of target
                        return;
                }
                nFromOffsetY = -nY;
                nToOffsetY = 0;
                nTargetH = (pFrom->height + nY < pTo->height) ? (pFrom->height + nY) : pTo->height;
        } else {
                nFromOffsetY = 0;
                nToOffsetY = nY;
                nTargetH = (pFrom->height + nToOffsetY > pTo->height) ? (pTo->height - nToOffsetY) : pFrom->height;
        }

        // linesize[] could not be equal to the actual width of the video due to alignment
        // (refer to final arg of av_frame_get_buffer())
        nFromOffset = pFrom->linesize[0] * nFromOffsetY + nFromOffsetX;
        nToOffset = pTo->linesize[0] * nToOffsetY + nToOffsetX;

        // copy Y plane data from src frame to dst frame
        for (int32_t i = 0; i < nTargetH; ++i) {
                std::memcpy(pTo->data[0] + pTo->linesize[0] * i + nToOffset,
                            pFrom->data[0] + pFrom->linesize[0] * i + nFromOffset,
                            nTargetW);
        }

        // UV plane

        int32_t nFromUVOffsetX  = nFromOffsetX / 2;     // row UV data offset of pFrom
        int32_t nFromUVOffsetY  = nFromOffsetY / 2;     // colume UV data offset of pFrom
        int32_t nToUVOffsetX    = nToOffsetX / 2;       // row UV data offset of pTo
        int32_t nToUVOffsetY    = nToOffsetY / 2;       // colume UV data offset of pTo
        int32_t nTargetUVX      = nTargetW / 2;         // width of mix area for UV
        int32_t nTargetUVY      = nTargetH / 2;         // height of mix area for UV

        int32_t nFromUVOffset   = pFrom->linesize[1] * nFromUVOffsetY + nFromUVOffsetX;
        int32_t nToUVOffset     = pTo->linesize[1] * nToUVOffsetY + nToUVOffsetX;

        // copy UV plane data from src to dst
        for (int32_t j = 0; j < nTargetUVY; ++j) {
                std::memcpy(pTo->data[1] + nToUVOffset + pTo->linesize[1] * j, pFrom->data[1] + nFromUVOffset + pFrom->linesize[1] * j, nTargetUVX);
                std::memcpy(pTo->data[2] + nToUVOffset + pTo->linesize[2] * j, pFrom->data[2] + nFromUVOffset + pFrom->linesize[2] * j, nTargetUVX);
        }
}

//
// AudioMixer
//

AudioMixer::AudioMixer()
{
}

AudioMixer::~AudioMixer()
{
}

int AudioMixer::Mix(IN const std::vector<std::shared_ptr<MediaFrame>>& _frames, OUT std::shared_ptr<MediaFrame>& _pOut)
{
        // get a silent audio frame
        auto pMuted = std::make_shared<MediaFrame>();
        pMuted->Stream(STREAM_AUDIO);
        pMuted->Codec(CODEC_AAC);
        pMuted->AvFrame()->nb_samples = AudioResampler::DEFAULT_FRAME_SIZE;
        pMuted->AvFrame()->format = AudioResampler::SAMPLE_FMT;
        pMuted->AvFrame()->channels = AudioResampler::CHANNELS;
        pMuted->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
        pMuted->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
        av_frame_get_buffer(pMuted->AvFrame(), 0);
        av_samples_set_silence(pMuted->AvFrame()->data, 0, pMuted->AvFrame()->nb_samples, pMuted->AvFrame()->channels,
                               (AVSampleFormat)pMuted->AvFrame()->format);

        // mixer works here
        for (auto& pFrame : _frames) {
                if (pFrame == nullptr) {
                        Warn("internal: got 1 null frame, something was wrong");
                        continue;
                }
                SimpleMix(pFrame, pMuted);
        }

        _pOut = pMuted;

        return 0;
}

void AudioMixer::SimpleMix(IN const std::shared_ptr<MediaFrame>& _pFrom, OUT std::shared_ptr<MediaFrame>& _pTo)
{
        AVFrame* pF = _pFrom->AvFrame();
        AVFrame* pT = _pTo->AvFrame();

        int16_t* pF16 = (int16_t*)pF->data[0];
        int16_t* pT16 = (int16_t*)pT->data[0];

        for (int i = 0; i < pF->linesize[0] && i < pT->linesize[0]; i+=2) {
                int32_t nMixed = *pF16 + *pT16;
                if (nMixed > 32767) {
                        nMixed = 32767;
                } else if (nMixed < -32768) {
                        nMixed = -32768;
                }
                *pT16 = static_cast<int16_t>(nMixed);

                pF16++;
                pT16++;
        }
}
