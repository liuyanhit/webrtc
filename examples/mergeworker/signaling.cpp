
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
	Info("================= construct rtcconnection =========");
}

void RtcConn::Start() {
	Info("Rtc Connection Start ==============");
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
    Info("================================ onFrame");
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
	Info("=============================== onData");
    Verbose("OnFrameAudio %zu %d %d %zu", number_of_frames, sample_rate, bits_per_sample, number_of_channels);
    if (OnAudio != nullptr)
        OnAudio(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
}

// TODO
void RtcConn::OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {

	Json::Value value;
	Json::Reader reader;
	if (!reader.parse(report->ToJson(), value)) {
		Error("parse stats report string failed");
		return;
	}

	auto now = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time_);
	last_time_ = now;
	for (uint32_t i = 0; i < value.size(); ++i) {
		stats_report_.width_ = value[i]["frameWidth"].asInt();
		stats_report_.height_ = value[i]["frameHeight"].asInt();
		stats_report_.bytesSent_ = value[i]["bytesSent"].asInt();
		stats_report_.bytesReceived_ = value[i]["bytesReceived"].asInt();
	}

	gS->onStatsUpdate(duration);
}

int RtcConn::AddRef()const {
	return rtc::AtomicOps::Increment(&ref_count_);
}

int RtcConn::Release()const {
	int count = rtc::AtomicOps::Decrement(&ref_count_);
	return count;
}

rtc::scoped_refptr<RtcConn> Signaling::NewRtcConn(const Json::Value& m) {
	Info("new rtc connection...");
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

Signaling::~Signaling() {
	stats_thread_exit_flag_.store(true);	
	if (stats_thread_.joinable()) {
		stats_thread_.join();
	}
	notify_thread_exit_flag_.store(true);
	if (notify_thread_.joinable()) {
		notify_thread_.join();
	}
}

void Signaling::addRtcConn(const std::string& id, rtc::scoped_refptr<RtcConn> c) {
    std::lock_guard<std::mutex> lock(rtcconn_map_lock_);
	Info("add RtcConnection(id:%s)", id.c_str());
	rtcconn_map_.insert(std::pair<std::string, rtc::scoped_refptr<RtcConn>>(id, c));
    //rtcconn_map_[id] = c;
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
	Info("handle subres");
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
	stats_thread_exit_flag_.store(false);
	stats_thread_ = std::thread(std::bind(&Signaling::getStatistics, this));
	notify_thread_exit_flag_.store(false);
	notify_thread_ = std::thread(std::bind(&Signaling::notifyStats, this));
    h_.connect(url);
    h_.run();
}

// TODO
void Signaling::getStatistics() {
	while(stats_thread_exit_flag_.load() != true) {
		std::cout << "get statistics =====================" << std::endl;
		if (rtcconn_map_.size() == 0) {
			// TODO
			std::cout << "no available rtccon!!" << std::endl;
			break;
		}
		auto it = rtcconn_map_.begin();
		while (it != rtcconn_map_.end()) {
			it->second->wrtcconn_->GetStats(it->second.get());
			++it;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
	}
}

void Signaling::onStatsUpdate(std::chrono::duration<uint64_t, std::milli> duration) {
	using namespace std::chrono;
	std::lock_guard<std::mutex> lock_map(rtcconn_map_lock_);
	if (rtcconn_map_.size() == 0) {
		// TODO
		std::cout << "no valid rtc connection!!=================" << std::endl;
		return;
	}
	auto it = rtcconn_map_.begin();
	uint64_t input = 0;
	uint64_t output = 0;
	int32_t res = 0;
	while (it != rtcconn_map_.end()) {
		input += it->second->stats_report_.bytesReceived_;
		output += it->second->stats_report_.bytesSent_;
		res += (it->second->stats_report_.width_ * it->second->stats_report_.height_);
		++it;
	}
	auto now = high_resolution_clock::now();
	auto now_s = duration_cast<seconds>(now.time_since_epoch()).count();
	std::lock_guard<std::mutex> lock_json_stats(stats_json_lock_);	
	statsJson_["roomId"] = tokenjson_["roomId"];
	statsJson_["publishUrl"] = tokenjson_["publishUrl"];
	statsJson_["jobId"] = tokenjson_["jobId"];
	statsJson_["app"] = tokenjson_["app"];
	statsJson_["uid"] = tokenjson_["uid"];
	statsJson_["time"] = now_s;
	statsJson_["duration"] = duration_cast<milliseconds>(duration).count();
	statsJson_["inputBytes"] = Json::UInt64(input);
	statsJson_["outputBytes"] = Json::UInt64(output);
	statsJson_["resolution"] = Json::Int(res);
}

void Signaling::notifyStats()
{
	auto httpcli = std::make_unique<CurlWrapper>();
	httpcli->CurlEasySetopt<int>(CURLOPT_TIMEOUT, 5);
	httpcli->CurlEasySetopt<const char*>(CURLOPT_URL, posturl_.c_str());
	auto slist = httpcli->CurlSlistAppend("Content-Type:application/json;charset=UTF-8");	
	httpcli->CurlEasySetopt<struct curl_slist*>(CURLOPT_HTTPHEADER, slist);
	while(notify_thread_exit_flag_.load() != true) {
		auto content = rtc::JsonValueToString(statsJson_);
		//if (statsJson_["uid"].asString().size() == 0) {
			// TODO
			//std::cout << "no valid content in jsonstats" << std::endl;
			//std::this_thread::sleep_for(std::chrono::milliseconds(5000)); continue;
		//}
		httpcli->CurlEasySetopt<const char*>(CURLOPT_POSTFIELDS, content.c_str());
		auto res = httpcli->CurlEasyPerform();
		if (res != CURLE_OK) {
			std::cout << "curl failed !!! res = " << res << std::endl;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	}
}
