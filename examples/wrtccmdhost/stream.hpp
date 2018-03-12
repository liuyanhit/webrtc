#ifndef __STREAM_HPP__
#define __STREAM_HPP__

#include "packet.hpp"
#include "rtc_base/json.h"

/*
libmuxer-new={"output_opt":{...} } res={"id":"xxx", "output_stream_id":"xxx"}
libmuxer-set-output-opt={...}
libmuxer-add-input={"id":"xx", "stream_id":"xx", "opt":{...}}
libmuxer-set-input-opt={"id":"xx", "stream_id":"xx", "opt":{...}}
libmuxer-remove-input={"id":"xxx", "stream_id":"xx"}
stream-add-sink={"id":"xxx", "type":"ffmpeg/libmuxer_rtmp", "url":"rtmp://" or "xxx.flv"} res={"id":"xxx"}
stream-remove-sink={"id":"xxx", "sink_id":"xxx"} res={}
stream-stat={"id":"xxx"} res={"w":240, "h":480, "avg_fps":23, "sample_rate":44000, "sink":[...]}
*/

class SinkObserver {
public:
    SinkObserver() : id_(newReqId()) {}
    virtual void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame) = 0;
    virtual void OnStart() {};
    virtual void OnStop() {};
    virtual void OnStat(Json::Value& stat) {};
    std::string Id() { return id_; }
    virtual ~SinkObserver() {}

private:
    std::string id_;
};

class SinkAddRemover {
public:
    virtual bool AddSink(SinkObserver *sink) = 0;
    virtual bool RemoveSink(const std::string& id) = 0;
    virtual ~SinkAddRemover() {}
};

class Stream: public SinkAddRemover {
public:
    Stream(const std::string& id) : id_(id), sinks_map_(), sinks_map_lock_() {}
    Stream() : id_(newReqId()), sinks_map_(), sinks_map_lock_() {}
    bool AddSink(SinkObserver *sink);
    bool RemoveSink(const std::string& id);
    void SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame);
    std::string Id() { return id_; }

private:
    std::string id_;
    std::map<std::string, SinkObserver*> sinks_map_;
    std::mutex sinks_map_lock_;
};

class StreamManager {
public:
    void AddStream(const Stream *stream);
    Stream *FindStream(const std::string& id);

private:
};

#endif