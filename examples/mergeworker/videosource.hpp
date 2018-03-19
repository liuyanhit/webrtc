#ifndef __WRTC_VIDEO_SOURCE_HPP__
#define __WRTC_VIDEO_SOURCE_HPP__

#include "input.hpp"
#include "media/base/fakevideocapturer.h"

class MediaSource {
public:
	MediaSource(){};
	virtual ~MediaSource(){};
	virtual void Start(){};
};

// TODO
class VideoSource : public MediaSource {
public:
	VideoSource();
	VideoSource(const std::string&);
	~VideoSource();

	void Start();

private:
	std::string input_url_;
	std::unique_ptr<muxer::AvReceiver> receiver_;
	std::function<int(const std::unique_ptr<muxer::MediaPacket>)> packet_handler_;
};

class FakeVideoSource: public MediaSource {
public:
	FakeVideoSource();
	~FakeVideoSource();

	void SetVideoFormat(int, int, int, int);

	void Start();
	std::function<void(const webrtc::VideoFrame& frame, int, int)> onFrame;
	
private:
	std::unique_ptr<cricket::FakeVideoCapturer> fake_video_capture_;
	int width_;
	int height_;
	int fps_;
	int format_;
};

#endif
