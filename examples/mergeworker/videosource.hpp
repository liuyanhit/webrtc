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
	VideoSource() = default;
	~VideoSource() = default;
	void Start(){};
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
