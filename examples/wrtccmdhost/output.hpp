#ifndef __OUTPUT_HPP__
#define __OUTPUT_HPP__

#include "common.hpp"

namespace muxer
{
        typedef const std::function<int(IN const std::shared_ptr<MediaPacket>&)> EncoderHandlerType;
        class AvEncoder
        {
        public:
                AvEncoder();
                ~AvEncoder();
                int Encode(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
                void Bitrate(IN int nBitrate);
        private:
                int Init(IN const std::shared_ptr<MediaFrame>& pFrame);
                int Preset(IN const std::shared_ptr<MediaFrame>& pFrame);
                int PresetAac(IN const std::shared_ptr<MediaFrame>& pFrame);
                int PresetH264(IN const std::shared_ptr<MediaFrame>& pFrame);
                int InitAudioResampling(IN const std::shared_ptr<MediaFrame>& pFrame);
                int ResampleAudio(IN const std::shared_ptr<MediaFrame>& pInFrame, OUT std::shared_ptr<MediaFrame>& pOutFrame);
                int EncodeAac(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
                int EncodeH264(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
        private:
                AVCodecContext* pAvEncoderContext_ = nullptr;
                bool bIsEncoderAvailable_ = false;
                std::vector<char> frameBuffer_;
                SwrContext* pSwr_ = nullptr; // for resampling

                int nBitrate_ = 0;
        };

        class AvSender
        {
        public:
                virtual int Send(IN const std::string& url, IN const std::shared_ptr<MediaPacket>& pPacket) = 0;
                virtual ~AvSender() {}
        };

        //
        // FlvMeta
        //

        class FlvMeta
        {
        public:
                FlvMeta();
                void PutByte(IN uint8_t nValue);
                void PutBe16(IN uint16_t nValue);
                void PutBe24(IN uint32_t nValue);
                void PutBe32(IN uint32_t nValue);
                void PutBe64(IN uint64_t nValue);
                void PutAmfString(IN const char* pString);
                void PutAmfString(IN const std::string& string);
                void PutAmfDouble(IN double dValue);
                void PutPacketMetaInfo(IN const MediaPacket& packet);
                void PutDefaultMetaInfo();
        public:
                char* Data() const;
                int Size() const;
        private:
                std::vector<char> payload_;
        };

        //
        // AdtsHeader
        //

        class AdtsHeader
        {
        public:
                unsigned int nSyncWord = 0;
                unsigned int nId = 0;
                unsigned int nLayer = 0;
                unsigned int nProtectionAbsent = 0;
                unsigned int nProfile = 0;
                unsigned int nSfIndex = 0;
                unsigned int nPrivateBit = 0;
                unsigned int nChannelConfiguration = 0;
                unsigned int nOriginal = 0;
                unsigned int nHome = 0;

                unsigned int nCopyrightIdentificationBit = 0;
                unsigned int nCopyrigthIdentificationStart = 0;
                unsigned int nAacFrameLength = 0;
                unsigned int nAdtsBufferFullness = 0;

                unsigned int nNoRawDataBlocksInFrame = 0;
        public:
                bool Parse(IN const char* pBuffer);
                bool GetBuffer(OUT char* pBuffer, OUT size_t* pSize = nullptr);
                void Dump();
        };

        //
        // H264Nalu
        //

        class H264Nalu
        {
        public:
                H264Nalu(const char* pBuffer, int nSize);
                char* Data() const;
                int Size() const;
                int Type() const;
        private:
                std::vector<char> payload_;
        };

        //
        // RtmpSender
        //

        class RtmpSender final : public AvSender
        {
        public:
                RtmpSender();
                ~RtmpSender();
                virtual int Send(IN const std::string& url, IN const std::shared_ptr<MediaPacket>& pPacket);

        private:
                // send audio
                int SendMp3Packet(IN const MediaPacket& packet);
                int SendAacPacket(IN const MediaPacket& packet);
                int SendAacConfig(IN const MediaPacket& packet);
                char GetAudioTagHeader(IN char nFormat, IN char nRate, IN char nSize, IN char nType);
                char GetAudioTagHeader(IN char nFormat, IN const MediaPacket& packet);
                bool IsAac(char chHeader);

                // send video
                int SendH264Packet(IN const MediaPacket& packet);
                int SendH264Nalus(IN const std::vector<H264Nalu>& nalus);
                int SendH264Config(IN const MediaPacket& packet);
                int GetH264Nalu(IN const MediaPacket& packet, OUT std::vector<H264Nalu>& nalus);
                int SendH264Nalus(IN const std::vector<H264Nalu>& nalus, IN const MediaPacket& packet);
                int GetH264StartCodeLen(IN const char* pData);
                int SendH264NonIdr(IN const std::vector<char>& buffer, IN const MediaPacket& packet);
                int SendH264Idr(IN const std::vector<char>& buffer, IN const MediaPacket& packet);
                int SendH264Packet(IN const char* pData, IN size_t nSize, IN size_t nTimestamp,
                                   IN bool bIsKeyFrame, size_t nCompositionTime);

                // send meta
                int SendStreamMetaInfo(IN const MediaPacket& packet);
                int SendMetadataPacket(IN const MediaPacket& packet);

                // send chunksize
                int SendChunkSize(IN size_t nSize);

                // send data
                int SendAudioPacket(IN const char chHeader, IN const char* pData, IN size_t nSize, IN size_t nTimestamp);

                // type 0 or type 1 packet
                int SendPacketLarge(IN unsigned int nPacketType, IN const char* pData,
                                    IN size_t nSize, IN size_t nTimestamp);
                int SendPacketMedium(IN unsigned int nPacketType, IN const char* pData,
                                     IN size_t nSize, IN size_t nTimestamp);
                int SendPacket(IN unsigned int nPacketType, IN const char* pData, IN size_t nSize,
                               IN size_t nTimestamp);

                // final call to send data
                int SendRawPacket(IN unsigned int nPacketType, IN int nHeaderType,
                                  IN const char* pData, IN size_t nSize, IN size_t nTimestamp);

                // calculate timestamp
                ssize_t AccTimestamp(IN const size_t& nNow, OUT size_t& nBase, OUT size_t& nSequence);

        private:
                // channel
                const int CHANNEL_CHUNK = 0x2;
                const int CHANNEL_VIDEO = 0x4;
                const int CHANNEL_META  = 0x5;
                const int CHANNEL_AUDIO = 0x6;

                // packet count
                size_t nVideoSequence_ = 0;
                size_t nVideoTimestamp_ = 0;
                size_t nAudioSequence_ = 0;
                size_t nAudioTimestamp_ = 0;
                size_t nDataSequence_ = 0;
                size_t nDataTimestamp_ = 0;

                // sequence header
                bool bH264ConfigSent_ = false;
                bool bAacConfigSent_ = false;

                // metadata
                FlvMeta metadata_;
                bool bVideoMetaSent_ = false;
                bool bAudioMetaSent_ = false;

                // URL target rtmp server
                std::string url_;

                // RTMP object from RTMP dump
                RTMP* pRtmp_ = nullptr;

                // sequence header
                std::shared_ptr<H264Nalu> pSps_ = nullptr;
                std::shared_ptr<H264Nalu> pPps_ = nullptr;
        };

        class Output : public OptionMap
        {
        public:
                Output(IN const std::string& name);
                ~Output();
                std::string Name();
                void Start(IN const std::string& url);
                void Stop();
                bool Push(IN std::shared_ptr<MediaFrame>& pFrame);
        private:
                std::string name_;
                std::thread sender_;
                std::atomic<bool> bSenderExit_;
                SharedQueue<std::shared_ptr<MediaFrame>> muxedQ_;
        };
}

#endif
