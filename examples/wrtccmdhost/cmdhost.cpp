#include "cmdhost.hpp"
#include "stream.hpp"
#include <stdlib.h>

const std::string kId = "id";
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
    void OnAddStream(const std::string& id, Stream *stream) {
        {
            std::lock_guard<std::mutex> lock(h_->streams_map_lock_);
            h_->streams_map_[stream->Id()] = stream;
        }
        Json::Value res;
        res["id"] = id;
        res["stream_id"] = stream->Id();
        writeMessage(mtOnConnAddStream, res);
    }
    void OnRemoveStream(const std::string& id, const std::string& stream_id) {
        Json::Value res;
        res["id"] = id;
        res["stream_id"] = stream_id;
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
    auto conn = new WRTCConn(pc_factory_, rtcconf, conn_observer);
    conn_observer->id_ = conn->ID();
    
    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        conn_map_[conn->ID()] = conn;
    }

    Json::Value res;
    res[kId] = conn->ID();
    observer->OnSuccess(res);
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

void CmdHost::handleNewLibmuxer(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    int w = jsonAsInt(req["w"]);
    int h = jsonAsInt(req["h"]);
    auto m = new muxer::AvMuxer(w, h);
    auto id = newReqId();
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        muxers_map_[id] = m;
    }
    Json::Value res;
    res[kId] = id;
    observer->OnSuccess(res);
}

static void libmuxerSetInputOpt(const std::shared_ptr<muxer::Input>& lin, const Json::Value& opt) {
    if (opt.isObject()) {
        auto w = opt["w"];
        if (!w.empty()) {
            lin->SetOption("w", jsonAsInt(w));
        }
        auto h = opt["h"];
        if (!h.empty()) {
            lin->SetOption("h", jsonAsInt(h));
        }
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
    }
}

void CmdHost::handleLibmuxerAddInput(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }
    auto id = jsonAsString(req["stream_id"]);

    Stream *stream = NULL;
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        stream = streams_map_[id];
    }
    if (stream == NULL) {
        observer->OnFailure(errInvalidParams, "stream not found");
        return;
    }

    m->AddInput(id, stream);
    libmuxerSetInputOpt(m->FindInput(id), req["opt"]);

    Json::Value res;
    res[kId] = id;
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