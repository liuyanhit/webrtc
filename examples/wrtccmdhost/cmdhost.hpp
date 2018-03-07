#include "msgpump.hpp"
#include "wrtcconn.hpp"

#include "muxer.hpp"
#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"
#include "rtc_base/refcount.h"

class CmdHost {
public:
    CmdHost();

    void Run();

    class CmdDoneObserver: public rtc::RefCountInterface {
    public:
        virtual void OnSuccess(Json::Value& res) = 0;
        virtual void OnFailure(int code, const std::string& error) = 0;
    };

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;

    rtc::Thread wrtc_work_thread_;
    rtc::Thread wrtc_signal_thread_;

    std::mutex conn_map_lock_;
    std::map<std::string, WRTCConn*> conn_map_;

    MsgPump* msgpump_;

    void writeMessage(const std::string& type, const Json::Value& msg);
    WRTCConn *checkConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);

    void handleCreateOfferSetLocalDesc(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleReq(const std::string& type, const Json::Value& req);
};
