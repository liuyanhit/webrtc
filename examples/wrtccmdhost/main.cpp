#include "cmdhost.hpp"
#include "rtc_base/flags.h"
#include "rtc_base/flags.h"
#include "rtc_base/logging.h"

unsigned int muxer::global::nLogLevel = 4;
DEFINE_int(logLevel, 4, "log level");
DEFINE_int(wrtcLogLevel, 0, "wrtc log level");

int main(int argc, char **argv) {
    if (rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, false) != 0) {
        rtc::FlagList::Print(NULL, false);        
        return -1;
    }

    muxer::global::nLogLevel = FLAG_logLevel;

    rtc::LoggingSeverity rtcls = rtc::LS_NONE;
    switch (FLAG_wrtcLogLevel) {
    case 1: rtcls = rtc::LS_ERROR; break;
    case 2: rtcls = rtc::LS_WARNING; break;
    case 3: rtcls = rtc::LS_INFO; break;
    case 4: rtcls = rtc::LS_VERBOSE; break;
    case 5: rtcls = rtc::LS_SENSITIVE; break;
    default: rtcls = rtc::LS_NONE; break;
    }
    rtc::LogMessage::LogToDebug(rtcls);

    Verbose("Started");

    CmdHost cmdhost;
    cmdhost.Run();

    return 0;
}
