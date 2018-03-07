#include "cmdhost.hpp"
#include "rtc_base/flags.h"
#include "common_types.h"

unsigned int muxer::global::nLogLevel = 4;
DEFINE_int(logLevel, 4, "log level");

int main(int argc, char **argv) {
    if (rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, false) != 0) {
        rtc::FlagList::Print(NULL, false);        
        return -1;
    }

    muxer::global::nLogLevel = FLAG_logLevel;

    Verbose("Started");

    CmdHost cmdhost;
    cmdhost.Run();

    return 0;
}
