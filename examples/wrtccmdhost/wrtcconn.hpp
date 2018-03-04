#ifndef __WRTCCONN_HPP__
#define __WRTCCONN_HPP__

#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"

class WRTCConn
{
public:
    WRTCConn(rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc);

private:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
};

#endif
