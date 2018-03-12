#include "wrtcconn.hpp"
#include "rtc_base/callback.h"

class WRTCStream: public Stream, rtc::VideoSinkInterface<webrtc::VideoFrame>, webrtc::AudioTrackSinkInterface {
public:
    WRTCStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) : Stream(stream->label()) {
        webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
        webrtc::AudioTrackVector atracks = stream->GetAudioTracks();
        if (!vtracks.empty()) {
            webrtc::VideoTrackInterface* track = vtracks[0];
            Info("AddVideoTrackSink state=%d enabled=%d", track->state(), track->enabled());
            track->AddOrUpdateSink(this, rtc::VideoSinkWants());
        }
        if (!atracks.empty()) {
            webrtc::AudioTrackInterface *track = atracks[0];
            Info("AddAudioSink");
            track->AddSink(this);
        }
        Info("AddSinkDone");
    }

    void OnFrame(const webrtc::VideoFrame& video_frame) {
        Info("OnFrameVideo");
    }

    void OnData(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames) 
    {
        Info("OnFrameAudio %zu %d %d %zu", number_of_frames, sample_rate, bits_per_sample, number_of_channels);
    }
};

class PeerConnectionObserver: public webrtc::PeerConnectionObserver {
public:
    PeerConnectionObserver(const std::string& id, WRTCConn::ConnObserver* conn_observer) : id_(id), conn_observer_(conn_observer) {}
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
        //Info("OnSignalingChange");
    }
    void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
        webrtc::AudioTrackVector atracks = stream->GetAudioTracks();
        Info("OnAddStream thread=%p vtracks=%lu atracks=%lu id=%s", rtc::Thread::Current(), vtracks.size(), atracks.size(), stream->label().c_str());

        conn_observer_->OnAddStream(id_, new WRTCStream(stream));
    }
    void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, 
            const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) 
    {
    }
    void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        conn_observer_->OnRemoveStream(id_, stream->label());
    }
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {}
    void OnRenegotiationNeeded() {}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        conn_observer_->OnIceConnectionChange(new_state);
    }
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
        conn_observer_->OnIceCandidate(candidate);
    }
    virtual ~PeerConnectionObserver() {}
    std::string id_;
    WRTCConn::ConnObserver* conn_observer_;
};

std::string WRTCConn::ID() {
    return id_;
}

WRTCConn::WRTCConn(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory, 
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf,
    WRTCConn::ConnObserver* conn_observer
) {
    id_ = newReqId();
    pc_ = pc_factory->CreatePeerConnection(rtcconf, nullptr, nullptr, nullptr, new PeerConnectionObserver(id_, conn_observer));
}

class SetLocalDescObserver: public webrtc::SetSessionDescriptionObserver {
public:
    SetLocalDescObserver(
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer,
        std::string desc
    ) : observer_(observer), desc_(desc) {}

    void OnSuccess() {
        observer_->OnSuccess(desc_);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~SetLocalDescObserver() {}

    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
    std::string desc_;
};

class CreateOfferSetDescObserver: public webrtc::CreateSessionDescriptionObserver {
public:
    CreateOfferSetDescObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    ) : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        std::string descstr;
        desc->ToString(&descstr);
        pc_->SetLocalDescription(new rtc::RefCountedObject<SetLocalDescObserver>(observer_, descstr), desc);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~CreateOfferSetDescObserver() {}

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
};

void WRTCConn::CreateOfferSetLocalDesc(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt, 
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
) {
    pc_->CreateOffer(new rtc::RefCountedObject<CreateOfferSetDescObserver>(pc_, observer), offeropt);
}

void WRTCConn::SetRemoteDesc(
    webrtc::SessionDescriptionInterface* desc,
    rtc::scoped_refptr<WRTCConn::SetDescObserver> observer
) {
    pc_->SetRemoteDescription(observer, desc);
}

class CreateAnswerObserver: public webrtc::CreateSessionDescriptionObserver {
public:
    CreateAnswerObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    ) : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        std::string descstr;
        desc->ToString(&descstr);
        pc_->SetLocalDescription(new rtc::RefCountedObject<SetLocalDescObserver>(observer_, descstr), desc);        
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }   

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
};

class SetDescCreateAnswerObserver: public webrtc::SetSessionDescriptionObserver {
public:
    SetDescCreateAnswerObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer,
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt
    ) : pc_(pc), observer_(observer), answeropt_(answeropt) {}

    void OnSuccess() {
        pc_->CreateAnswer(new rtc::RefCountedObject<CreateAnswerObserver>(pc_, observer_), answeropt_);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~SetDescCreateAnswerObserver() {}

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt_;
};

void WRTCConn::SetRemoteDescCreateAnswer(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt, 
    webrtc::SessionDescriptionInterface* desc,
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
) {
    pc_->SetRemoteDescription(new rtc::RefCountedObject<SetDescCreateAnswerObserver>(pc_, observer, answeropt), desc);
}

bool WRTCConn::AddIceCandidate(webrtc::IceCandidateInterface* candidate) {
    return pc_->AddIceCandidate(candidate);
}
