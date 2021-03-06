#include "cmdhost.hpp"
#include "stream.hpp"
#include "output.hpp"
#include <stdlib.h>

const std::string kId = "id";
const std::string kStreamId = "stream_id";
const std::string kCode = "code";
const std::string kError = "error";
const std::string kSdp = "sdp";
const std::string kCandidate = "candidate";
const int errConnNotFound = 10002;
const std::string errConnNotFoundString = "conn not found";
const int errInvalidParams = 10003;
const std::string errInvalidParamsString = "invalid params";

const std::string kSdpMLineIndex = "sdpMLineIndex";
const std::string kSdpMid = "sdpMid";

const std::string mtEcho = "echo";
const std::string mtNewConn = "new-conn";
const std::string mtCreateOfferSetLocalDesc = "create-offer-set-local-desc";
const std::string mtSetRemoteDesc = "set-remote-desc";
const std::string mtSetRemoteDescCreateAnswer = "set-remote-desc-create-answer";
const std::string mtOnIceCandidate = "on-ice-candidate";
const std::string mtOnIceConnectionChange = "on-ice-conn-state-change";
const std::string mtAddIceCandidate = "add-ice-candidate";
const std::string mtOnConnAddStream = "on-conn-add-stream";
const std::string mtOnConnRemoveStream = "on-conn-remove-stream";
const std::string mtNewLibMuxer = "new-libmuxer";
const std::string mtLibmuxerAddInput = "libmuxer-add-input";
const std::string mtLibmuxerSetInputsOpt = "libmuxer-set-inputs-opt";
const std::string mtLibmuxerRemoveInput = "libmuxer-remove-input";
const std::string mtStreamAddSink = "stream-add-sink";
const std::string mtNewCanvasStream = "new-canvas-stream";
const std::string mtNewUrlStream = "new-url-stream";
const std::string mtConnAddStream = "conn-add-stream";
const std::string mtConnStats = "conn-stats";
const std::string mtSinkStats = "sink-stats";

static void parseOfferAnswerOpt(const Json::Value& v, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& opt) {
    auto audio = v["audio"];
    if (audio.isBool() && audio.asBool()) {
        opt.offer_to_receive_audio = 1;
    }
    auto video = v["video"];
    if (video.isBool() && video.asBool()) {
        opt.offer_to_receive_video = 1;
    }
}

void CmdHost::Run() {
    wrtc_signal_thread_.Start();
    wrtc_work_thread_.Start();
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        &wrtc_work_thread_, &wrtc_work_thread_, &wrtc_signal_thread_, 
        nullptr, nullptr, nullptr
    );
    Info("wrtc_work_thread_ %p", &wrtc_work_thread_);
    Info("wrtc_signal_thread_ %p", &wrtc_signal_thread_);
    msgpump_->Run();
}

void CmdHost::writeMessage(const std::string& type, const Json::Value& msg) {
    msgpump_->WriteMessage(type, msg);
}

static std::string iceStateToString(webrtc::PeerConnectionInterface::IceConnectionState state) {
    switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
        return "new";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
        return "checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
        return "connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        return "completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
        return "failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
        return "disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        return "closed";
    default:
        return "unknown";
    }
}

class ConnObserver: public WRTCConn::ConnObserver {
public:
    ConnObserver(CmdHost *h) : h_(h) {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
        std::string sdp;
        candidate->ToString(&sdp);
        Json::Value res;
        Json::Value c;
        c[kSdpMid] = candidate->sdp_mid();
        c[kSdpMLineIndex] = std::to_string(candidate->sdp_mline_index());
        c[kCandidate] = sdp;
        res[kCandidate] = rtc::JsonValueToString(c);
        writeMessage(mtOnIceCandidate, res);
    }
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        Json::Value res;
        res["state"] = iceStateToString(new_state);
        writeMessage(mtOnIceConnectionChange, res);
    }
    void OnAddStream(const std::string& id, const std::string& stream_id, Stream *stream) {
        {
            std::lock_guard<std::mutex> lock(h_->streams_map_lock_);
            h_->streams_map_[stream_id] = stream;
        }
        Json::Value res;
        res[kId] = id;
        res[kStreamId] = stream_id;
        writeMessage(mtOnConnAddStream, res);
    }
    void OnRemoveStream(const std::string& id, const std::string& stream_id) {
        Json::Value res;
        res[kId] = id;
        res[kStreamId] = stream_id;
        writeMessage(mtOnConnRemoveStream, res);
    }
    void writeMessage(const std::string& type, Json::Value& res) {
        res[kId] = id_;
        h_->writeMessage(type, res);
    }
    virtual ~ConnObserver() {}
    CmdHost *h_;
    std::string id_;
};

void CmdHost::handleNewConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf = {};

    auto ice_servers = req["ice_servers"];

    if (ice_servers.isArray()) {
        webrtc::PeerConnectionInterface::IceServer icesrv = {};

        for (auto ice_server : ice_servers) {
            if (ice_server.isObject()) {
                auto urls = ice_server["urls"];
                if (urls.isArray()) {
                    for (auto url : urls) {
                        if (url.isString()) {
                            auto s = url.asString();
                            icesrv.urls.push_back(s);
                            Verbose("NewConnIceAddUrl %s", s.c_str());
                        }
                    }
                }
            }
        }

        rtcconf.servers.push_back(icesrv);
    }

    auto conn_observer = new ConnObserver(this);
    auto conn = new WRTCConn(pc_factory_, rtcconf, conn_observer, &wrtc_signal_thread_);
    conn_observer->id_ = conn->ID();
    
    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        conn_map_[conn->ID()] = conn;
    }

    Json::Value res;
    res[kId] = conn->ID();
    observer->OnSuccess(res);
}

Stream* CmdHost::checkStream(const std::string& id, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = NULL;
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        stream = streams_map_[id];
    }
    if (stream == NULL) {
        observer->OnFailure(errInvalidParams, "stream not found");
        return NULL;
    }
    return stream;
}

WRTCConn* CmdHost::checkConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto id = req[kId];
    if (!id.isString()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return NULL;
    }

    WRTCConn *conn = NULL;
    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        conn = conn_map_[id.asString()];
    }
    if (conn == NULL) {
        observer->OnFailure(errConnNotFound, errConnNotFoundString);
        return NULL;    
    }

    return conn;
}

class CreateDescObserver: public WRTCConn::CreateDescObserver {
public:
    CreateDescObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnSuccess(const std::string& desc) {
        Json::Value res;
        res[kSdp] = desc;
        observer_->OnSuccess(res);
    }
    void OnFailure(const std::string& error) {
        observer_->OnFailure(errInvalidParams, error);
    }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleCreateOfferSetLocalDesc(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt = {};
    parseOfferAnswerOpt(req, offeropt);
    conn->CreateOfferSetLocalDesc(offeropt, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

class SetDescObserver: public WRTCConn::SetDescObserver {
public:
    SetDescObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnSuccess() {
        Json::Value res;
        observer_->OnSuccess(res);
    }
    void OnFailure(const std::string& error) {
        observer_->OnFailure(errInvalidParams, error);
    }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleSetRemoteDesc(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    std::string sdp = jsonAsString(req[kSdp]);
    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription("answer", sdp, &err); 
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    conn->SetRemoteDesc(desc, new rtc::RefCountedObject<SetDescObserver>(observer));
}

void CmdHost::handleSetRemoteDescCreateAnswer(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    std::string sdp = jsonAsString(req[kSdp]);
    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription("offer", sdp, &err); 
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt = {};
    parseOfferAnswerOpt(req, answeropt);
    conn->SetRemoteDescCreateAnswer(answeropt, desc, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

void CmdHost::handleAddIceCandidate(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    std::string cs = jsonAsString(req[kCandidate]);
    Json::Reader jr;
    Json::Value c;
    if (!jr.parse(cs.c_str(), cs.c_str()+cs.size(), c, false)) {
        observer->OnFailure(errInvalidParams, "parse candidate failed");
        return;
    }

    std::string sdp_mid = jsonAsString(c[kSdpMid]);
    int sdp_mlineindex = atoi(jsonAsString(c[kSdpMLineIndex]).c_str());
    std::string sdp = jsonAsString(c[kCandidate]);

    webrtc::SdpParseError err;
    auto candidate = webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &err);
    if (!candidate) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    if (!conn->AddIceCandidate(candidate)) {
        observer->OnFailure(errInvalidParams, "AddIceCandidate failed");
        return;
    }

    Json::Value v;
    observer->OnSuccess(v);
}

muxer::AvMuxer* CmdHost::checkLibmuxer(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto id = req[kId];
    if (!id.isString()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return NULL;
    }

    muxer::AvMuxer *m = NULL;
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        m = muxers_map_[id.asString()];
    }

    if (m == NULL) {
        observer->OnFailure(errInvalidParams, "libmuxer not found");
        return NULL;
    }

    return m;
}

class LibmuxerOutputStream: public Stream {
public:
    LibmuxerOutputStream() {}
};

void CmdHost::handleNewLibmuxer(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    int w = jsonAsInt(req["w"]);
    int h = jsonAsInt(req["h"]);

    if (w == 0 || h == 0) {
        observer->OnFailure(errInvalidParams, "invalid w or h");
        return;
    }

    auto m = new muxer::AvMuxer(w, h);
    auto id = newReqId();
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        muxers_map_[id] = m;
    }

    bool audioOnly = jsonAsBool(req["audioOnly"]);
    m->audioOnly_.store(audioOnly);

    auto stream = new LibmuxerOutputStream();
    auto stream_id = newReqId();
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = stream;
    }
    m->AddOutput(stream_id, stream);

    m->Start();

    Json::Value res;
    res[kId] = id;
    res["output_stream_id"] = stream_id;
    observer->OnSuccess(res);
}

static void libmuxerSetInputOpt(const std::shared_ptr<muxer::Input>& lin, const Json::Value& opt) {
    if (opt.isObject()) {
        auto w = jsonAsInt(opt["w"]);
        if (w <= 0) {
            Fatal("invalid w");
        }
        lin->SetOption("w", w);
        auto h = jsonAsInt(opt["h"]);
        if (h <= 0) {
            Fatal("invalid h");
        }
        lin->SetOption("h", h);
        auto x = opt["x"];
        if (!x.empty()) {
            lin->SetOption("x", jsonAsInt(x));
        }
        auto y = opt["y"];
        if (!y.empty()) {
            lin->SetOption("y", jsonAsInt(y));
        }
        auto z = opt["z"];
        if (!z.empty()) {
            lin->SetOption("z", jsonAsInt(z));
        }
        auto hidden = opt["hidden"];
        if (!hidden.empty()) {
            lin->SetOption("hidden", jsonAsBool(hidden));
        }
        auto muted = opt["muted"];
        if (!muted.empty()) {
            lin->SetOption("muted", jsonAsBool(muted));
        }
    }
}

void CmdHost::handleLibmuxerAddInput(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }

    auto id = jsonAsString(req[kStreamId]);
    Stream *stream = checkStream(id, observer);
    if (stream == NULL) {
        return;
    }

    m->AddInput(id, stream);
    libmuxerSetInputOpt(m->FindInput(id), req["opt"]);

    Json::Value res;
    res[kId] = id;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerRemoveInput(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }
    auto id = jsonAsString(req[kStreamId]);
    m->RemoveInput(id);

    Json::Value res;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerSetInputsOpt(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }

    auto inputs = req["inputs"];
    if (inputs.isArray()) {
        for (auto input : inputs) {
            auto id = jsonAsString(input[kId]);
            auto lin = m->FindInput(id);
            if (lin != nullptr) {
                libmuxerSetInputOpt(lin, input["opt"]);
            }
        }
    }

    Json::Value res;
    observer->OnSuccess(res);
}

void CmdHost::handleStreamAddSink(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto stream = checkStream(jsonAsString(req[kId]), observer);
    if (stream == NULL) {
        return;
    }

    auto url = jsonAsString(req["url"]);
    if (url == "") {
        observer->OnFailure(errInvalidParams, "url invalid");
        return;
    }

    auto sinkid = newReqId();
    auto sink = new muxer::RtmpSink(url, std::make_shared<XLogger>(sinkid));

    auto kbps = jsonAsInt(req["kbps"]);
    if (kbps != 0) {
        sink->videoKbps = kbps;
    }

    stream->AddSink(sinkid, sink);

    Json::Value res;
    res[kId] = sinkid;
    observer->OnSuccess(res);
}

class CanvasStream: public Stream {
public:
    CanvasStream() : fps_(25), w_(320), h_(240), bg_(0xff0000) {
    }

    void Start() {
        auto gen_video = [this] {
            int ts_ms = 0;
            while (exit_.load() == false) {
                std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
                frame->Stream(muxer::STREAM_VIDEO);
                frame->Codec(muxer::CODEC_H264);

                auto avframe = frame->AvFrame();
                avframe->format = AV_PIX_FMT_YUV420P;
                avframe->height = h_.load();
                avframe->width = w_.load();
                avframe->pts = ts_ms;
                av_frame_get_buffer(avframe, 32);

                uint32_t bg = bg_.load();
                uint8_t nR = bg >> 16;
                uint8_t nG = (bg >> 8) & 0xff;
                uint8_t nB = bg & 0xff;
                uint8_t nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
                uint8_t nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
                uint8_t nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);

                memset(avframe->data[0], nY, avframe->linesize[0] * avframe->height);
                memset(avframe->data[1], nU, avframe->linesize[1] * (avframe->height/2));
                memset(avframe->data[2], nV, avframe->linesize[2] * (avframe->height/2));

                SendFrame(frame);

                double sleep_s = 1.0/((double)fps_.load());
                usleep((useconds_t)(sleep_s*1e6));
                ts_ms += int(sleep_s*1e3);
            }
        };
        video_thread_ = std::thread(gen_video);

        auto gen_audio = [this] {
            int ts_ms = 0;
            auto sample_rate = 8000;
            double dur = 0.01;
            auto nb_samples = (int)((double)sample_rate*dur);
            double sint = 0.0;

            while (exit_.load() == false) {
                std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
                frame->Stream(muxer::STREAM_AUDIO);
                frame->Codec(muxer::CODEC_AAC);

                auto avframe = frame->AvFrame();
                avframe->format = AV_SAMPLE_FMT_S16;
                avframe->channel_layout = AV_CH_LAYOUT_MONO;
                avframe->sample_rate = sample_rate;
                avframe->nb_samples = nb_samples;
                avframe->pts = ts_ms;
                av_frame_get_buffer(avframe, 0);

                int16_t *p = (int16_t*)avframe->data[0];
                for (int i = 0; i < nb_samples; i++) {
                    p[i] = (int16_t)(sin(sint*2*M_PI*440)*0x7fff);
                    sint += 1.0/(double)sample_rate;
                }
                //memset(avframe->data[0], 0, avframe->linesize[0]);

                SendFrame(frame);

                usleep((useconds_t)(dur*0.5*1e6));
                ts_ms += int(dur*1e3);
            }
        };
        audio_thread_ = std::thread(gen_audio);
    }

    void Stop() {
        exit_.store(true);
        if (video_thread_.joinable()) {
            video_thread_.join();
        }
        if (audio_thread_.joinable()) {
            audio_thread_.join();
        }
    }

    std::thread video_thread_;
    std::thread audio_thread_;
    std::atomic<bool> exit_;
    std::atomic<int> fps_;
    std::atomic<int> w_;
    std::atomic<int> h_;
    std::atomic<uint32_t> bg_;
};

void CmdHost::handleNewUrlStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto url = jsonAsString(req["url"]);

    auto stream_id = newReqId();

    muxer::Input* input = new muxer::Input(std::make_shared<XLogger>(stream_id), "");
    input->nativeRate_ = true;
    input->doRescale_ = false;
    input->doResample_ = false;
    //input->resampler_.frameSize = int(muxer::AudioResampler::SAMPLE_RATE * 0.01);
    input->Start(url);

    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = input;
    }
    Json::Value res;
    res[kId] = stream_id;
    observer->OnSuccess(res);    
}

void CmdHost::handleNewCanvasStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    int fps = jsonAsInt(req["fps"]);
    int w = jsonAsInt(req["w"]);
    int h = jsonAsInt(req["h"]);
    uint32_t bg = (int)jsonAsInt(req["bg"]);

    CanvasStream *stream = new CanvasStream();

    if (fps) {
        stream->fps_.store(fps);
    }
    if (w) {
        stream->w_.store(w);
    }
    if (h) {
        stream->h_.store(h);
    }
    stream->bg_.store(bg);
    stream->Start();

    auto stream_id = newReqId();
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = stream;
    }
    Json::Value res;
    res[kId] = stream_id;
    observer->OnSuccess(res);
}

void CmdHost::handleConnAddStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }
    
    auto stream = checkStream(jsonAsString(req["stream_id"]), observer);
    if (stream == NULL) {
        return;
    }

    if (!conn->AddStream(stream)) {
        observer->OnFailure(errInvalidParams, "add stream failed");
        return;
    }

    observer->OnSuccess();
}

class ConnGetStatsObserver: public webrtc::RTCStatsCollectorCallback {
public:
    ConnGetStatsObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
        Json::Value res;
        res["stats"] = report->ToJson();
        observer_->OnSuccess(res);
    }

    int AddRef() const { return 0; }
    int Release() const { return 0; }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleConnStats(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }
    conn->GetStats(new ConnGetStatsObserver(observer));
}

void CmdHost::handleSinkStats(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto stream = checkStream(jsonAsString(req[kId]), observer);
    if (stream == NULL) {
        return;
    }

    auto sink = stream->FindSink(jsonAsString(req["sink_id"]));
    if (sink == NULL) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    int64_t bytes = 0;
    sink->OnStatBytes(bytes);

    Json::Value res;
    res["bytes"] = int(bytes);
    observer->OnSuccess(res);
}

class CmdDoneWriteResObserver: public CmdHost::CmdDoneObserver {
public:
    CmdDoneWriteResObserver(rtc::scoped_refptr<MsgPump::Request> req) : req_(req) {}
    void OnSuccess(Json::Value& res) {
        res[kCode] = 0;
        req_->WriteResponse(res);
    }
    void OnFailure(int code, const std::string& error) {
        Json::Value res;
        res[kCode] = code;
        res[kError] = error;
        req_->WriteResponse(res);
    }
    rtc::scoped_refptr<MsgPump::Request> req_;
};

void CmdHost::handleMsg(const std::string& type, const Json::Value& body) {
}

void CmdHost::handleReq(rtc::scoped_refptr<MsgPump::Request> req) {
    auto type = req->type;
    if (type == mtEcho) {
        Json::Value res = req->body;
        req->WriteResponse(res);
    } else if (type == mtNewConn) {
        handleNewConn(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtCreateOfferSetLocalDesc) {
        handleCreateOfferSetLocalDesc(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSetRemoteDesc) {
        handleSetRemoteDesc(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSetRemoteDescCreateAnswer) {
        handleSetRemoteDescCreateAnswer(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtAddIceCandidate) {
        handleAddIceCandidate(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewLibMuxer) {
        handleNewLibmuxer(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerAddInput) {
        handleLibmuxerAddInput(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerSetInputsOpt) {
        handleLibmuxerSetInputsOpt(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerRemoveInput) {
        handleLibmuxerRemoveInput(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtStreamAddSink) {
        handleStreamAddSink(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewCanvasStream) {
        handleNewCanvasStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtConnAddStream) {
        handleConnAddStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewUrlStream) {
        handleNewUrlStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));       
    } else if (type == mtConnStats) {
        handleConnStats(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSinkStats) {
        handleSinkStats(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));        
    }
}

class MsgPumpObserver: public MsgPump::Observer {
public:
    MsgPumpObserver(CmdHost *h) : h_(h) {}
    void OnRequest(rtc::scoped_refptr<MsgPump::Request> req) {
        h_->handleReq(req);
    }
    void OnMessage(const std::string& type, const Json::Value& body) {
        h_->handleMsg(type, body);
    }
    CmdHost* h_;
};

CmdHost::CmdHost(): conn_map_() {
    msgpump_ = new MsgPump(new MsgPumpObserver(this));
}