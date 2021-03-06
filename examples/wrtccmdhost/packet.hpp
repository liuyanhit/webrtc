#ifndef __PACKET_HPP__
#define __PACKET_HPP__

#include "common.hpp"

namespace muxer
{
        class MediaPacket
        {
        public:
                MediaPacket(IN const AVStream& pAvStream, IN const AVPacket* pAvPacket);
                ~MediaPacket();
                MediaPacket();
                MediaPacket(const MediaPacket&) = delete; // no copy for risk concern

                // get raw AV structs
                AVPacket* AvPacket() const;
                AVCodecParameters* AvCodecParameters() const;

                // pts and dts
                uint64_t Pts() const;
                void Pts(IN uint64_t);
                uint64_t Dts() const;
                void Dts(IN uint64_t);

                // stream and codec
                StreamType Stream() const;
                void Stream(IN StreamType);
                CodecType Codec() const;
                void Codec(IN CodecType);

                // data fields
                char* Data()const;
                int Size() const;

                // util
                void Print() const;
                void Dump(const std::string& title = "") const;

                // video
                int Width() const;
                int Height() const;
                void Width(int);
                void Height(int);
                bool IsKey() const;
                void SetKey();

                // audio
                int SampleRate() const;
                int Channels() const;
                void SampleRate(int);
                void Channels(int);

        private:
                AVPacket* pAvPacket_ =  nullptr;
                AVCodecParameters* pAvCodecPar_ = nullptr;

                // save following fields seperately
                StreamType stream_;
                CodecType codec_;

                // video specific
                int nWidth_ = -1, nHeight_ = -1;
                int nSampleRate_ = -1, nChannels_ = -1;
        };

        class MediaFrame
        {
        public:
                MediaFrame(IN const AVFrame* pFrame);
                MediaFrame();
                MediaFrame(IN int nSamples, IN int nChannels, IN AVSampleFormat format, IN bool bSilence = false);
                MediaFrame(IN int nWidth, IN int nHeight, IN AVPixelFormat format, IN int nColor = -1);
                MediaFrame(const MediaFrame&);
                ~MediaFrame();
                AVFrame* AvFrame() const;

                StreamType Stream() const;
                void Stream(IN StreamType);
                CodecType Codec() const;
                void Codec(IN CodecType);

                void ExtraBuffer(unsigned char* pBuf); // TODO: delete, will use AudioResampler instead

                void Print() const;

                int X() const;
                void X(IN int);
                int Y() const;
                void Y(IN int);
                int Z() const;
                void Z(IN int);
        private:
                AVFrame* pAvFrame_;

                StreamType stream_;
                CodecType codec_;

                unsigned char* pExtraBuf_ = nullptr;

                int nX_ = 0, nY_ = 0, nZ_ = 0;
        };
}

#endif
