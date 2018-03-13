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
    }

    void OnFrame(const webrtc::VideoFrame& rtcframe) {
        //Info("OnFrameVideo");

        std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
        frame->Stream(muxer::STREAM_VIDEO);
        frame->Codec(muxer::CODEC_H264);
        frame->AvFrame()->format = AV_PIX_FMT_YUV420P;
        frame->AvFrame()->height = rtcframe.height();
        frame->AvFrame()->width = rtcframe.width();
        av_frame_get_buffer(frame->AvFrame(), 32);

        auto rtcfb = rtcframe.video_frame_buffer();
        auto i420 = rtcfb->ToI420();

        const uint8_t* rtcdata[3] = {
            i420->DataY(),
            i420->DataU(),
            i420->DataV(),
        };
        int rtclinesize[3] = {
            i420->StrideY(),
            i420->StrideU(),
            i420->StrideV(),
        };
        int height[3] = {
            rtcframe.height(),
            i420->ChromaHeight(),
            i420->ChromaHeight(),
        };

        for (int i = 0; i < 3; i++) {
            yuv::CopyLine(frame->AvFrame()->data[i], frame->AvFrame()->linesize[i], 
                    rtcdata[i], rtclinesize[i], height[i]
            );
        }

        SendFrame(frame);
    }

    void OnData(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames) 
    {
        //Info("OnFrameAudio %zu %d %d %zu", number_of_frames, sample_rate, bits_per_sample, number_of_channels);

        auto frame = std::make_shared<muxer::MediaFrame>();
        frame->Stream(muxer::STREAM_AUDIO);
        frame->Codec(muxer::CODEC_AAC);
        frame->AvFrame()->format = AV_SAMPLE_FMT_S16;
        frame->AvFrame()->channel_layout = AV_CH_LAYOUT_MONO;
        frame->AvFrame()->sample_rate = sample_rate;
        frame->AvFrame()->channels = number_of_channels;
        frame->AvFrame()->nb_samples = number_of_frames;
        av_frame_get_buffer(frame->AvFrame(), 0);

        memcpy(frame->AvFrame()->data[0], audio_data, bits_per_sample/8*number_of_frames);
        SendFrame(frame);

        DebugPCM("/tmp/rtc.orig.s16", audio_data, bits_per_sample/8*number_of_frames);
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
