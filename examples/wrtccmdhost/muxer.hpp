#ifndef __MUXER_HPP__
#define __MUXER_HPP__

#include "common.hpp"
#include "input.hpp"
#include "output.hpp"

namespace muxer
{
        class VideoMuxer
        {
        public:
                VideoMuxer(IN int nW, IN int nH);
                ~VideoMuxer();
                int Mux(IN std::vector<std::shared_ptr<MediaFrame>>& frames, OUT std::shared_ptr<MediaFrame>&);
                void BgColor(int nRGB);
        private:
                void Overlay(IN const std::shared_ptr<MediaFrame>& pFrom, OUT std::shared_ptr<MediaFrame>& pTo);
                int nCanvasW_ = 0, nCanvasH_ = 0;
                int nBackground_ = 0x000000; // black
        };

        class AudioMixer
        {
        public:
                AudioMixer();
                ~AudioMixer();
                int Mix(IN const std::vector<std::shared_ptr<MediaFrame>>& frames, OUT std::shared_ptr<MediaFrame>&);
        private:
                void SimpleMix(IN const std::shared_ptr<MediaFrame>& pFrom, OUT std::shared_ptr<MediaFrame>& pTo);
        };

        class AvMuxer : public OptionMap
        {
        public:
                AvMuxer(IN int nWidth, IN int nHeight);
                ~AvMuxer();

                int AddOutput(IN const std::string& name, IN FrameSender* stream);
                int ModOutputOption(IN const std::string& name, IN const std::string& key, IN const std::string& val = "");
                int ModOutputOption(IN const std::string& name, IN const std::string& key, IN int nVal);
                int DelOutputOption(IN const std::string& name, IN const std::string& key);
                int RemoveOutput(IN const std::string& name);

                int AddInput(IN const std::string& name, IN SinkAddRemover *stream);
                int ModInputOption(IN const std::string& name, IN const std::string& key, IN const std::string& val = "");
                int ModInputOption(IN const std::string& name, IN const std::string& key, IN int nVal);
                int DelInputOption(IN const std::string& name, IN const std::string& key);
                int RemoveInput(IN const std::string& name);

                int Start();
        public:
                std::shared_ptr<Input> FindInput(IN const std::string& name);
                std::shared_ptr<Output> FindOutput(IN const std::string& name);
                void FeedOutputs(IN std::shared_ptr<MediaFrame>& pFrame);

                std::atomic<bool> audioOnly_;

        private:
                SharedQueue<std::shared_ptr<Input>> inputs_;
                SharedQueue<std::shared_ptr<Output>> outputs_;

                std::thread videoMuxerThread_;
                std::thread audioMuxerThread_;

                VideoMuxer videoMuxer_;
                AudioMixer audioMixer_;

                // internal clock to generate pts
                std::mutex clockLck_;
                uint64_t nInitClock_ = 0;
        };
}

#endif
