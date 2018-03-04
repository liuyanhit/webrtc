#include "cmdhost.hpp"

CmdHost::CmdHost(): conn_map_() {
    auto on_msg = [this](const std::string& type, const Json::Value& req) {
        handleReq(type, req);
    };
    msgpump_ = new MsgPump(on_msg);
}

void CmdHost::Run() {
    wrtc_signal_thread_.Start();
    wrtc_work_thread_.Start();
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        &wrtc_work_thread_, &wrtc_work_thread_, &wrtc_signal_thread_, 
        nullptr, nullptr, nullptr
    );
    msgpump_->Run();
}

WRTCConn* CmdHost::newConn() {
    //auto conn = new WRTCConn();
    //return conn;
    return NULL;
}

void CmdHost::handleCreateOfferReq(const Json::Value& req, Json::Value& res) {
}

void CmdHost::handleReq(const std::string& type, const Json::Value& req) {
}
