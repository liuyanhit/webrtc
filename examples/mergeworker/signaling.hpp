#ifndef __WRTCINPUT_HPP__
#define __WRTCINPUT_HPP__

#include "uWS.h"

#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"
#include "audio_device.h"
#include "common.hpp"

class RtcConn : public 
    webrtc::CreateSessionDescriptionObserver, 
    webrtc::PeerConnectionObserver,
    rtc::VideoSinkInterface<webrtc::VideoFrame>,
    webrtc::AudioTrackSinkInterface
{
public:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> wrtcconn_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> rtcfactory_;

    std::string streamid_;
    std::string connid_;

    std::function<void (const std::string& c)> send_candidate_;
    std::function<void (const std::string& sdp)> send_sdp_;
    std::function<void (const webrtc::VideoFrame& video_frame)> OnVideo;
    std::function<void (const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames)> OnAudio;

    RtcConn(rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> rtcfactory);
    
    void Start();
    void OnRecvIceCandidate(const std::string& c);
    void OnRecvSDP(const std::string& sdpstr);
    void OnSuccess();
    void OnSuccess(webrtc::SessionDescriptionInterface* desc);
    void OnFailure(const std::string& error);
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state);
    void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream);
    void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream);
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel);
    void OnRenegotiationNeeded();
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state);
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state);
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate);
    void OnFrame(const webrtc::VideoFrame& video_frame);
    void OnData(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames);
};

class WThread : public rtc::Thread {
public:
    WThread() {
    }

    void Join() {
        Thread::Join();
    }
};

class Signaling {
public:
    uWS::Hub h_;
    uWS::WebSocket<uWS::CLIENT> *wscli_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> rtcfactory_;

    WThread wrtc_work_thread_;
    WThread wrtc_signal_thread_;

    std::mutex rtcconn_map_lock_;
    std::map<std::string, rtc::scoped_refptr<RtcConn>> rtcconn_map_;

    bool is_demo_;
    Json::Value tokenjson_;
    std::string token_;

    Signaling();

    rtc::scoped_refptr<RtcConn> NewRtcConn(const Json::Value &m);

    void send(const char *type, const Json::Value& value);
    void onMessage(const std::string& type, const Json::Value& m);
    void onConnection();
    void connectRun(const std::string& url);

    void addRtcConn(const std::string& id, rtc::scoped_refptr<RtcConn> c);
    rtc::scoped_refptr<RtcConn> findRtcConn(const std::string& id);

    void authorize();
    void subscribe(const std::string& streamid);

    void handleWebRtcAnswer(const Json::Value& m);
    void handleWebRtcCandidate(const Json::Value& m);
    void handleSubRes(const Json::Value& m);
    void handleAuthRes(const Json::Value& m);
    void handleOnAddStream(const Json::Value& m);
    void handleOnRemoveStream(const Json::Value& m);
    void handlePing(const Json::Value& m);

    std::function<void (const Json::Value& value)> OnAddStream;
    std::function<void (const Json::Value& value)> OnRemoveStream;
};

extern Signaling *gS;

#endif
