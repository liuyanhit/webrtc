#include "msgpump.hpp"
#include "wrtcconn.hpp"

#include "muxer.hpp"
#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"

class CmdHost {
public:
    CmdHost();

    void Run();

private:
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;

    rtc::Thread wrtc_work_thread_;
    rtc::Thread wrtc_signal_thread_;

    std::mutex conn_map_lock_;
    std::map<std::string, WRTCConn*> conn_map_;

    MsgPump* msgpump_;

    WRTCConn* newConn();
    //rtc::scoped_refptr<WRTCConn> findConn(const std::string& id);

    void handleCreateOfferReq(const Json::Value& req, Json::Value& res);
    void handleReq(const std::string& type, const Json::Value& req);
};
