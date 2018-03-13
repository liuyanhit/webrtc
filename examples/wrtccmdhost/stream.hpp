#ifndef __STREAM_HPP__
#define __STREAM_HPP__

#include "packet.hpp"
#include "rtc_base/json.h"

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

class FrameSender {
public:
    virtual void SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame) = 0;
    virtual ~FrameSender() {}
};

class Stream: public SinkAddRemover, public FrameSender {
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

#endif