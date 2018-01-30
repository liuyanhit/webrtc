#include "packet.hpp"

using namespace muxer;

MediaPacket::MediaPacket(IN const AVStream& _avStream, IN const AVPacket* _pAvPacket)
{
        // save codec pointer
        pAvCodecPar_ = _avStream.codecpar;
        StreamType stream = static_cast<StreamType>(pAvCodecPar_->codec_type);
        if (stream != STREAM_AUDIO && stream != STREAM_VIDEO) {
                stream = STREAM_DATA;
        }
        Stream(stream);
        Codec(static_cast<CodecType>(pAvCodecPar_->codec_id));
        Width(pAvCodecPar_->width);
        Height(pAvCodecPar_->height);
        SampleRate(pAvCodecPar_->sample_rate);
        Channels(pAvCodecPar_->channels);

        // copy packet
        pAvPacket_ = const_cast<AVPacket*>(_pAvPacket);
}

MediaPacket::MediaPacket()
{
        pAvPacket_ = av_packet_alloc();
        av_init_packet(pAvPacket_);
}

MediaPacket::~MediaPacket()
{
        av_packet_free(&pAvPacket_);
}

AVPacket* MediaPacket::AvPacket() const
{
        return const_cast<AVPacket*>(pAvPacket_);
}

AVCodecParameters* MediaPacket::AvCodecParameters() const
{
        return pAvCodecPar_;
}

uint64_t MediaPacket::Pts() const
{
        return pAvPacket_->pts;
}

void MediaPacket::Pts(uint64_t _pts)
{
        pAvPacket_->pts = _pts;
}

uint64_t MediaPacket::Dts() const
{
        return pAvPacket_->dts;
}

void MediaPacket::Dts(uint64_t _dts)
{
        pAvPacket_->dts = _dts;
}

StreamType MediaPacket::Stream() const
{
        return stream_;
}

void MediaPacket::Stream(StreamType _type)
{
        stream_ = _type;
}

CodecType MediaPacket::Codec() const
{
        return codec_;
}

void MediaPacket::Codec(CodecType _type)
{
        codec_ = _type;
}

char* MediaPacket::Data()const
{
        return reinterpret_cast<char*>(pAvPacket_->data);
}

int MediaPacket::Size() const
{
        return static_cast<int>(pAvPacket_->size);
}

void MediaPacket::Print() const
{
        Info("packet: pts=%lu, dts=%lu, stream=%d, codec=%d, size=%lu",
             static_cast<unsigned long>(pAvPacket_->pts), static_cast<unsigned long>(pAvPacket_->dts),
             Stream(), Codec(), static_cast<unsigned long>(pAvPacket_->size));
}

void MediaPacket::Dump(const std::string& _title) const
{
        Debug("%spts=%lu, dts=%lu, stream=%d, codec=%d, size=%lu", _title.c_str(),
              static_cast<unsigned long>(pAvPacket_->pts), static_cast<unsigned long>(pAvPacket_->dts),
              Stream(), Codec(), static_cast<unsigned long>(pAvPacket_->size));
        global::PrintMem(Data(), Size());
}

int MediaPacket::Width() const
{
        return nWidth_;
}

int MediaPacket::Height() const
{
        return nHeight_;
}

void MediaPacket::Width(int _nValue)
{
        nWidth_ = _nValue;
}

void MediaPacket::Height(int _nValue)
{
        nHeight_ = _nValue;
}

int MediaPacket::SampleRate() const
{
        return nSampleRate_;
}

int MediaPacket::Channels() const
{
        return nChannels_;
}

void MediaPacket::SampleRate(int _nValue)
{
        nSampleRate_ = _nValue;
}

void MediaPacket::Channels(int _nValue)
{
        nChannels_ = _nValue;
}

bool MediaPacket::IsKey() const
{
        return ((pAvPacket_->flags & AV_PKT_FLAG_KEY) != 0);
}

void MediaPacket::SetKey()
{
        pAvPacket_->flags |= AV_PKT_FLAG_KEY;
}

//
// MediaFrame
//

MediaFrame::MediaFrame(IN const AVFrame* _pAvFrame)
{
        pAvFrame_ = const_cast<AVFrame*>(_pAvFrame);
}

MediaFrame::MediaFrame()
{
        pAvFrame_ = av_frame_alloc();
}

MediaFrame::~MediaFrame()
{
        av_frame_free(&pAvFrame_);

        if (pExtraBuf_ != nullptr) {
                av_free(pExtraBuf_);
                pExtraBuf_ = nullptr;
        }
}

AVFrame* MediaFrame::AvFrame() const
{
        return pAvFrame_;
}

StreamType MediaFrame::Stream() const
{
        return stream_;
}

void MediaFrame::Stream(StreamType _type)
{
        stream_ = _type;
}

CodecType MediaFrame::Codec() const
{
        return codec_;
}

void MediaFrame::Codec(CodecType _type)
{
        codec_ = _type;
}

void MediaFrame::ExtraBuffer(unsigned char* _pBuf)
{
        pExtraBuf_ = _pBuf;
}

void MediaFrame::Print() const
{
        Info("frame: pts=%lu, stream=%d, codec=%d, linesize=%lu",
             static_cast<unsigned long>(pAvFrame_->pts), Stream(), Codec(), static_cast<unsigned long>(pAvFrame_->linesize[0]));
}

int MediaFrame::X() const
{
        return nX_;
}

void MediaFrame::X(IN int _nX)
{
        nX_ = _nX;
}

int MediaFrame::Y() const
{
        return nY_;
}

void MediaFrame::Y(IN int _nY)
{
        nY_ = _nY;
}

int MediaFrame::Z() const
{
        return nZ_;
}

void MediaFrame::Z(IN int _nZ)
{
        nZ_ = _nZ;
}
