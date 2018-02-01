
#include "signaling.hpp"

class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    static DummySetSessionDescriptionObserver* Create() {
        return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
    }
    virtual void OnSuccess() {
        //LOG(INFO) << __FUNCTION__;
    }
    virtual void OnFailure(const std::string& error) {
        //LOG(INFO) << __FUNCTION__ << " " << error;
    }
protected:
    DummySetSessionDescriptionObserver() {}
    ~DummySetSessionDescriptionObserver() {}
};

RtcConn::RtcConn(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> rtcfactory
) : rtcfactory_(rtcfactory), streamid_(""), connid_("") {
}

void RtcConn::Start() {
    webrtc::PeerConnectionInterface::IceServer icesrv;
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf;
    /*
    icesrv.username = "";
    icesrv.password = "";
    icesrv.hostname = "";
    */
    //icesrv.tls_cert_policy = webrtc::PeerConnectionInterface::kTlsCertPolicySecure;
    icesrv.urls.push_back("stun:123.59.184.112:3478");
    //icesrv.urls.push_back("turn:123.59.184.112:3478");
    rtcconf.servers.push_back(icesrv);

    wrtcconn_ = rtcfactory_->CreatePeerConnection(rtcconf, nullptr, nullptr, nullptr, this);
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt;
    offeropt.offer_to_receive_audio = 1;
    offeropt.offer_to_receive_video = 1;
    wrtcconn_->CreateOffer((webrtc::CreateSessionDescriptionObserver *)this, offeropt);

    Info("RtcConnStarted");
}

void RtcConn::OnRecvIceCandidate(const std::string& c_) {
    Json::Value c;
    Json::Reader r;
    r.parse(c_.c_str(), c_.c_str()+c_.size(), c, false);

    std::string sdp_mid = c["sdpMid"].asString();
    int sdp_mlineindex = atoi(c["sdpMLineIndex"].asString().c_str());
    std::string sdp = c["candidate"].asString();

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
    if (!candidate.get()) {
      Fatal("Can't parse received candidate message: %s", error.description.c_str());
    }
    if (!wrtcconn_->AddIceCandidate(candidate.get())) {
      Fatal("Failed to apply the received candidate");
    }
}

void RtcConn::OnRecvSDP(const std::string& sdpstr) {
    webrtc::SdpParseError err;
    webrtc::SessionDescriptionInterface* desc = CreateSessionDescription("answer", sdpstr, &err);    
    if (!desc) {
        Fatal("create answer failed. error line: %s, error desc: %s", err.line.c_str(), err.description.c_str());
    }
    wrtcconn_->SetRemoteDescription(new rtc::RefCountedObject<DummySetSessionDescriptionObserver>(), desc);
}

void RtcConn::OnSuccess() {
}

void RtcConn::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    std::string sdp_str;
    desc->ToString(&sdp_str);
    send_sdp_(sdp_str);
    wrtcconn_->SetLocalDescription(new rtc::RefCountedObject<DummySetSessionDescriptionObserver>(), desc);
}

void RtcConn::OnFailure(const std::string& error) {
    Info("CreateDescOnFailure %s", error.c_str()); 
}

void RtcConn::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
    Info("OnSignalingChange %d", new_state);
}

void RtcConn::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
    webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
    webrtc::AudioTrackVector atracks = stream->GetAudioTracks();

    Info("RtcOnAddStream vtracks=%lu atracks=%lu", vtracks.size(), atracks.size());

    if (!vtracks.empty()) {
        webrtc::VideoTrackInterface* track = vtracks[0];
        Info("AddSinkVideo");
        track->AddOrUpdateSink(this, rtc::VideoSinkWants());
    }
    if (!atracks.empty()) {
        Info("AddSinkAudio");
        webrtc::AudioTrackInterface *track = atracks[0];
        track->AddSink(this);
    }
}

void RtcConn::OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, 
            const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams)
{
    Info("RtcOnAddTrack streams=%lu", streams.size());

    for (auto stream : streams) {
        webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
        webrtc::AudioTrackVector atracks = stream->GetAudioTracks();

        Info("RtcOnAddTrack vtracks=%lu atracks=%lu", vtracks.size(), atracks.size());
    }
}

void RtcConn::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
    Info("OnRemoveStream");
}

void RtcConn::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
}

void RtcConn::OnRenegotiationNeeded() {
}    

void RtcConn::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    Info("OnIceConnectionChange %d", new_state);
}

void RtcConn::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
}

void RtcConn::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string cs;
    candidate->ToString(&cs);

    Json::Value c;
    c["sdpMid"] = candidate->sdp_mid();
    c["sdpMLineIndex"] = std::to_string(candidate->sdp_mline_index());
    c["candidate"] = cs;

    send_candidate_(rtc::JsonValueToString(c));
}

void RtcConn::OnFrame(const webrtc::VideoFrame& video_frame) {
    Verbose("OnFrameVideo");
    if (OnVideo != nullptr)
        OnVideo(video_frame);
}

void RtcConn::OnData(const void* audio_data,
    int bits_per_sample,
    int sample_rate,
    size_t number_of_channels,
    size_t number_of_frames) 
{
    Verbose("OnFrameAudio %zu %d %d %zu", number_of_frames, sample_rate, bits_per_sample, number_of_channels);
    if (OnAudio != nullptr)
        OnAudio(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
}

rtc::scoped_refptr<RtcConn> Signaling::NewRtcConn(const Json::Value& m) {
    auto conn = new rtc::RefCountedObject<RtcConn>(rtcfactory_);

    std::string streamid = m["streamid"].asString();
    std::string connid = m["connid"].asString();
    conn->streamid_ = streamid;
    conn->connid_ = connid;

    conn->send_candidate_ = [conn, this](const std::string& c) {
        if (is_demo_) {
            Json::Value v;
            v["candidate"] = c;
            v["type"] = "candidate";
            v["option"] = "subscribe";
            send("candidate", v);
        } else {
            Json::Value v;
            v["streamid"] = conn->streamid_;
            v["connid"] = conn->connid_;
            v["candidate"] = c;
            send("webrtc-candidate", v);
        }
    };

    conn->send_sdp_ = [conn, this](const std::string& sdp) {
        if (is_demo_) {
            Json::Value v;
            v["sdp"] = sdp;
            v["type"] = "sdp";
            v["option"] = "subscribe";
            send("sdp", v);
        } else {
            Json::Value v;
            v["streamid"] = conn->streamid_;
            v["connid"] = conn->connid_;
            v["sdp"] = sdp;
            send("webrtc-offer", v);
        }
    };

    addRtcConn(streamid, conn);

    return conn;
}

Signaling::Signaling() {
    wrtc_signal_thread_.Start();
    wrtc_work_thread_.Start();

    rtcfactory_ = webrtc::CreatePeerConnectionFactory(
        &wrtc_work_thread_, &wrtc_work_thread_, &wrtc_signal_thread_, 
        nullptr, nullptr, nullptr
    );

    h_.onDisconnection([](uWS::WebSocket<uWS::CLIENT> *ws, int code, char *message, size_t length) {
        Info("WsDisconnected");
    });

    h_.onMessage([this](uWS::WebSocket<uWS::CLIENT> *ws, char *message, size_t length, uWS::OpCode opCode) {
        auto ls = std::string(message, length);
        Info("WsRecv %s", ls.c_str());

        std::string type;

        if (!is_demo_) {
            char *i = strchr(message, '=');
            if (i == NULL) {
                Fatal("singaling message type not found");
            }
            type = std::string(message, i-message);
            int shift = i-message+1;
            message += shift;
            length -= shift;
        }

        Json::Value m;
        Json::Reader r;
        r.parse(message, message+length, m, false);
        onMessage(type, m);
    });

    h_.onConnection([this](uWS::WebSocket<uWS::CLIENT> *ws, uWS::HttpRequest req) {
        Info("WsConnected");
        wscli_ = ws;
        onConnection();
    });
}

void Signaling::addRtcConn(const std::string& id, rtc::scoped_refptr<RtcConn> c) {
    std::lock_guard<std::mutex> lock(rtcconn_map_lock_);
    rtcconn_map_[id] = c;
}

rtc::scoped_refptr<RtcConn> Signaling::findRtcConn(const std::string& id) {
    std::lock_guard<std::mutex> lock(rtcconn_map_lock_);
    return rtcconn_map_[id];
}

void Signaling::send(const char *type, const Json::Value& value) {
    auto vs = rtc::JsonValueToString(value);
    std::string s = "";

    if (!is_demo_) {
        s += type;
        s += "=";
    }
    s += vs;

    Info("WsSend %s", s.c_str());
    wscli_->send(s.c_str());
}

void Signaling::authorize() {
    Json::Value v;
    v["token"] = token_;
    send("auth", v);
}

void Signaling::handleWebRtcCandidate(const Json::Value& m) {
    std::string streamid = m["streamid"].asString();
    auto conn = findRtcConn(streamid);
    conn->OnRecvIceCandidate(m["candidate"].asString());
}

void Signaling::handleWebRtcAnswer(const Json::Value& m) {
    std::string streamid = m["streamid"].asString();
    auto conn = findRtcConn(streamid);
    conn->OnRecvSDP(m["sdp"].asString());
}

void Signaling::handleAuthRes(const Json::Value& m) {
    RTC_CHECK(m["code"].asInt() == 0);

    Json::Value streams = m["streams"];
    for (const Json::Value &stream : streams) {
        std::string streamid = stream["streamid"].asString();
        subscribe(streamid);
    }
}

void Signaling::subscribe(const std::string& streamid) {
    Json::Value v;
    v["streamid"] = streamid;
    send("sub", v);
}

void Signaling::handleSubRes(const Json::Value& m) {
    int code = m["code"].asInt();
    if (code) {
        return;
    }

    if (OnAddStream) {
        OnAddStream(m);
    }
}

void Signaling::handleOnAddStream(const Json::Value& m) {
    std::string streamid = m["streamid"].asString();
    subscribe(streamid);
}

void Signaling::handleOnRemoveStream(const Json::Value& m) {
    if (OnRemoveStream) {
        OnRemoveStream(m);
    }
}

void Signaling::handlePing(const Json::Value& m) {
    Json::Value v;
    send("pong", v);
}

void Signaling::onMessage(const std::string& type, const Json::Value& m) {
    if (is_demo_) {
        auto conn = findRtcConn("");
        if (m["option"] == "subscribe") {
            if (m["type"] == "sdp") {
                conn->OnRecvSDP(m["sdp"].asString());
            } else if (m["type"] == "candidate") {
                conn->OnRecvIceCandidate(m["candidate"].asString());
            }
        }
        return;
    }

    if (type == "auth-res") {
        handleAuthRes(m);
    } else if (type == "sub-res") {
        handleSubRes(m);
    } else if (type == "webrtc-candidate") {
        handleWebRtcCandidate(m);
    } else if (type == "webrtc-answer") {
        handleWebRtcAnswer(m);
    } else if (type == "on-add-stream") {
        handleOnAddStream(m);
    } else if (type == "on-remove-stream") {
        handleOnRemoveStream(m);
    } else if (type == "ping") {
        handlePing(m);
    }

    return;
}

void Signaling::onConnection() {
    if (is_demo_) {
        if (OnAddStream) {
            Json::Value v;
            v["streamid"] = "";
            OnAddStream(v);
        }
        return;
    }

    authorize();
}

void Signaling::connectRun(const std::string& url) {
    h_.connect(url);
    h_.run();
}

