#include "videosource.hpp"

FakeVideoSource::FakeVideoSource()
	: fake_video_capture_(nullptr) 
{
	fake_video_capture_ = 
		std::make_unique<cricket::FakeVideoCapturer>(false);

	onFrame = [](const webrtc::VideoFrame& f, int w, int h)->void {
		//Info("height: %d", f.height());
		//Info("width: %d", f.width());
		//Info("timestamp: %d", f.timestamp());
	};
}

FakeVideoSource::~FakeVideoSource()
{
}

void FakeVideoSource::Start()
{
	uint32_t interval = cricket::VideoFormat::FpsToInterval(fps_)/1000000;
	if (!fake_video_capture_->GetCaptureFormat()) {
		Error("invalid video format");
		return;
	}

	if (!fake_video_capture_->IsRunning()) {
		Error("fake video capture is not running");
		return;
	}

	rtc::scoped_refptr<webrtc::I420Buffer> buffer(
		webrtc::I420Buffer::Create(width_, height_));
	buffer->InitializeData();

	uint32_t next_timestamp = 0;
	while(1) {
		auto frame = webrtc::VideoFrame(buffer, webrtc::kVideoRotation_0,next_timestamp / rtc::kNumNanosecsPerMicrosec);
		frame.set_timestamp(next_timestamp);
		onFrame(frame, width_, height_);
		next_timestamp += interval;
		std::this_thread::sleep_for(std::chrono::microseconds(interval));
	}

	return;
}

void FakeVideoSource::SetVideoFormat(int width, int height, int fps, int format) {
	height_ = height;
	width_ = width;
	fps_ = fps;
	format_ = format;
	uint32_t interval = cricket::VideoFormat::FpsToInterval(fps_) / 1000000;
	if (fake_video_capture_ != nullptr) {
		fake_video_capture_->Start(cricket::VideoFormat(width_, height_, interval, format_));
	}
}
