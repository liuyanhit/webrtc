#include "stream.hpp"

bool Stream::AddSink(LibMuxerSinkObserver *sink) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    auto it = sinks_map_.find(sinks->Id());
    if (it != sinks_map_.end()) {
        return false;
    }
    sinks_map_[sink->Id()] = sink;
    sink->OnStart();
    return true;
}

bool Stream::RemoveSink(const std::string& id) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    auto it = sinks_map_.find(sinks->Id());
    if (it == sinks_map_.end()) {
        return false;
    }
    auto sink = it->second;
    sink->OnStop();
    sinks_map_.erase(it);
}

void Stream::SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    for (auto it = sinks_map_.begin(); it != sinks_map_.end(); it++) {
        auto sink = it->second;
        sink->OnFrame(frame);
    }
}

void StreamManager::AddStream(const Stream *stream) {
    std::lock_guard<std::mutex> lock(streams_map_lock_);
    
}