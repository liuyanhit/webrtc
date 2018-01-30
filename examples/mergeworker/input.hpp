#ifndef __INPUT_HPP__
#define __INPUT_HPP__

#include "common.hpp"
#include "signaling.hpp"

namespace muxer
{
        // AvReceiver
        typedef const std::function<int(const std::unique_ptr<MediaPacket>)> PacketHandlerType;
        class AvReceiver
        {
        public:
                AvReceiver();
                ~AvReceiver();
                int Receive(IN const std::string& url, IN PacketHandlerType& callback);
        private:
                static int AvInterruptCallback(void* pContext);
        private:
                std::chrono::high_resolution_clock::time_point start_;
                long nTimeout_ = 10000; // 10 seconds timeout by default

                struct AVFormatContext* pAvContext_ = nullptr;
        };

        // AvDecoder
        typedef const std::function<int(const std::shared_ptr<MediaFrame>&)> FrameHandlerType;
        class AvDecoder
        {
        public:
                AvDecoder();
                ~AvDecoder();
                int Decode(IN const std::unique_ptr<MediaPacket>& pPacket, IN FrameHandlerType& callback);
        private:
                int Init(IN const std::unique_ptr<MediaPacket>& pPakcet);
        private:
                AVCodecContext* pAvDecoderContext_ = nullptr;
                bool bIsDecoderAvailable_ = false;
        };

        // Input
        class Input : public OptionMap
        {
        public:
                Input(IN const std::string& name);
                ~Input();
                std::string Name();

                void StartRtc(IN const Json::Value& m);
                void Start(IN const std::string& url);
                void Stop();

                // pop one video/audio
                bool GetVideo(OUT std::shared_ptr<MediaFrame>& pFrame, OUT size_t& nQlen);
                bool GetAudio(OUT std::shared_ptr<MediaFrame>& pFrame, OUT size_t& nQlen);

        private:
                // push one video/audio
                void SetAudio(const std::shared_ptr<MediaFrame>& pFrame);
                void SetVideo(const std::shared_ptr<MediaFrame>& pFrame);

        private:
                rtc::scoped_refptr<RtcConn> rtcconn_;
                bool isrtc_;
                static const size_t AUDIO_Q_LEN = 100;
                static const size_t VIDEO_Q_LEN = 10;
                std::string name_;
                std::thread receiver_;
                std::atomic<bool> bReceiverExit_;
                std::shared_ptr<MediaFrame> pLastVideo_ = nullptr;
                SharedQueue<std::shared_ptr<MediaFrame>> videoQ_;
                SharedQueue<std::shared_ptr<MediaFrame>> audioQ_;

                AudioResampler resampler_;
                std::vector<uint8_t> sampleBuffer_;
                std::mutex sampleBufferLck_;

                std::shared_ptr<VideoRescaler> pRescaler_ = nullptr;
        };
}

#endif
