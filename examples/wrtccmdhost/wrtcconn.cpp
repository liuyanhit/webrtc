#include "wrtcconn.hpp"

WRTCConn::WRTCConn(rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc) {
    pc_ = pc;
}
