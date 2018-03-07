#include "cmdhost.hpp"

const std::string kId = "id";
const std::string kCode = "code";
const std::string kError = "error";
const std::string kSdp = "sdp";
const int errConnNotFound = 10002;
const std::string errConnNotFoundString = "conn not found";
const int errInvalidParams = 10003;
const std::string errInvalidParamsString = "invalid params";

const std::string mtEcho = "echo";
const std::string mtNewConn = "new-conn";
const std::string mtCreateOfferSetLocalDesc = "create-offer-set-local-desc";
const std::string mtOnIceCandidate = "on-ice-candidate";
const std::string mtOnIceStateChange = "on-ice-state-change";

CmdHost::CmdHost(): conn_map_() {
    auto on_msg = [this](const std::string& type, const Json::Value& req) {
        handleReq(type, req);
    };
    msgpump_ = new MsgPump(on_msg);
}

void CmdHost::Run() {
    wrtc_signal_thread_.Start();
    wrtc_work_thread_.Start();
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        &wrtc_work_thread_, &wrtc_work_thread_, &wrtc_signal_thread_, 
        nullptr, nullptr, nullptr
    );
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
        Verbose("CmdHostOnIceCandidate");

        std::string sdp;
        candidate->ToString(&sdp);
        Json::Value res;
        res[kSdp] = sdp;
        res[kId] = id_;
        h_->writeMessage(mtOnIceCandidate, res);
    }
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        Json::Value res;
        res["state"] = iceStateToString(new_state);
        res[kId] = id_;
        h_->writeMessage(mtOnIceStateChange, res);
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
    conn_map_[conn->ID()] = conn;

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

    auto conn = conn_map_[id.asString()];
    if (conn == NULL) {
        observer->OnFailure(errConnNotFound, errConnNotFoundString);
        return NULL;    
    }

    return conn;
}

class CreateOfferSetLocalDescObserver: public WRTCConn::CreateDescObserver {
public:
    CreateOfferSetLocalDescObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnSuccess(const std::string& desc) {
        Json::Value res;
        res["sdp"] = desc;
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
    auto audio = req["audio"];
    if (audio.isBool() && audio.asBool()) {
        offeropt.offer_to_receive_audio = 1;
    }
    auto video = req["video"];
    if (video.isBool() && video.asBool()) {
        offeropt.offer_to_receive_video = 1;
    }

    conn->CreateOfferSetLocalDesc(offeropt, new rtc::RefCountedObject<CreateOfferSetLocalDescObserver>(observer));
}

class CmdDoneWriteResObserver: public CmdHost::CmdDoneObserver {
public:
    CmdDoneWriteResObserver(CmdHost *h, const std::string& type) : h_(h), type_(type) {}
    void OnSuccess(Json::Value& res) {
        res[kCode] = 0;
        writeMessage(res);
    }
    void OnFailure(int code, const std::string& error) {
        Json::Value res;
        res[kCode] = code;
        res[kError] = error;
        writeMessage(res);
    }
    void writeMessage(const Json::Value& res) {
        h_->writeMessage(type_+"-res", res);
    }
    CmdHost *h_;
    std::string type_;
};


void CmdHost::handleReq(const std::string& type, const Json::Value& req) {
    if (type == mtEcho) {
        writeMessage(type+"-res", req);
    } else if (type == mtNewConn) {
        handleNewConn(req, new rtc::RefCountedObject<CmdDoneWriteResObserver>(this, type));
    } else if (type == mtCreateOfferSetLocalDesc) {
        handleCreateOfferSetLocalDesc(req, new rtc::RefCountedObject<CmdDoneWriteResObserver>(this, type));
    }
}
