#include "signaling.hpp"
#include "muxer.hpp"
#include "jwt.hpp"
#include "rtc_base/flags.h"
#include "common_types.h"

Signaling *gS;

unsigned int muxer::global::nLogLevel = 4;

DEFINE_string(token, "", "jwt token");
DEFINE_string(publishUrl, "", "rtmp publish url");
DEFINE_string(demoWsUrl, "", "demo ws url");
DEFINE_bool(noMuxer, false, "don't use muxer");
DEFINE_int(logLevel, 4, "log level");

/*
class TracePrinter : public webrtc::TraceCallback {
public:
    void Print(webrtc::TraceLevel level, const char* message, int length) override {
        write(2, message, length);
        write(2, "\n", 1);
    }
};
*/

static std::string getUrlFromToken(const Json::Value& v) {
    std::string addr = v["roomserveraddr"].asString();
    return "ws://"+addr+"/signaling";
}

const double WindowHeight = 1280;
const double WindowWidth = 720;

void getWindowPos(int idx, int& x, int& y, int& w, int& h, int &z) {
    if (idx == 0) {
        x = 0;
        y = 0;
        w = (int)WindowWidth;
        h = (int)WindowHeight;
        z = 0;
        return;
    }

    double margin = WindowWidth * 0.05;
    double countPerRow = 4;
    double winW = (WindowWidth - margin*(countPerRow+1)) / countPerRow;
    double winH = winW * (WindowHeight/WindowWidth);
    double row = (double)(idx-1) / countPerRow;
    double col = (double)((idx-1) % (int)countPerRow);

    x = (int)((row+1)*margin + row*winW);
    y = (int)((col+1)*margin + col*winH);
    w = (int)winW;
    h = (int)winH;
    z = 1;
}

class PosManager {
public:
    std::mutex l_;
    std::deque<std::string> names_;
    muxer::AvMuxer *m;

    void update(int idx, const std::string& name) {
        int x, y, w, h, z;
        getWindowPos(idx, x, y, w, h, z);
        m->ModInputOption(name, "x", x);
        m->ModInputOption(name, "y", y);
        m->ModInputOption(name, "w", w);
        m->ModInputOption(name, "h", h);
        m->ModInputOption(name, "z", z);
    }

    void Add(const std::string& name) {
        std::lock_guard<std::mutex> lock(l_);
        int idx = names_.size();
        names_.push_back(name);
        update(idx, name);
    }

    void Remove(const std::string& name) {
        for (auto it = names_.begin(); it != names_.end(); it++) {
            if (*it == name) {
                    it = names_.erase(it);
                    break;
            }
        }
        for (int i = 0; i < (int)names_.size(); i++) {
            auto name = names_[i];
            update(i, name);
        }
    }
};

int main(int argc, char **argv) {
    /*
    webrtc::Trace::CreateTrace();
    webrtc::Trace::SetTraceCallback(new TracePrinter());
    webrtc::Trace::set_level_filter(webrtc::kTraceAll);
    */

    if (rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, false) != 0) {
        rtc::FlagList::Print(NULL, false);        
        return -1;
    }

    muxer::global::nLogLevel = FLAG_logLevel;

    std::string token(FLAG_token);
    Json::Value tokenjson;
    std::string publishUrl(FLAG_publishUrl);
    std::string demoWsUrl(FLAG_demoWsUrl);
    
    bool isDemo = demoWsUrl != "";

    if (!isDemo) {
        if (token == "" || publishUrl == "") {
            rtc::FlagList::Print(NULL, false);
            return -1;
        }
        int r = JwtDecode(token, tokenjson);
        if (r) {
            Fatal("parse token failed");
        }

        Info("publish url: %s", publishUrl.c_str());
    }

    auto m = std::make_unique<muxer::AvMuxer>(WindowWidth, WindowHeight);
    m->SetOption("bg", 0x333333);

    muxer::Option outopt;
    outopt.SetOption("vb", 1000);
    outopt.SetOption("ab", 64);

    if (publishUrl != "") {
        m->AddOutput("out1", publishUrl, outopt);
    }

    gS = new Signaling();
    gS->token_ = token;
    gS->tokenjson_ = tokenjson;
    gS->is_demo_ = isDemo;

    std::string wsurl;
    if (isDemo) {
        wsurl = demoWsUrl;
    } else {
        wsurl = getUrlFromToken(tokenjson);
    }

    auto posman = new PosManager();
    posman->m = m.get();

    gS->OnAddStream = [&](const Json::Value& v) {
        muxer::Option inopt;
        std::string streamid = v["streamid"].asString();
        Info("MergeOnAddStream %s", streamid.c_str());
        m->AddRtcInput(streamid, v, inopt);
        posman->Add(streamid);
    };

    gS->OnRemoveStream = [&](const Json::Value& v) {
        std::string streamid = v["streamid"].asString();
        Info("MergeOnRemoveStream %s", streamid.c_str());
        m->ModInputOption(streamid, muxer::options::hidden, true);
        posman->Remove(streamid);
    };

    if (!FLAG_noMuxer) {
        m->Start();
    }

    Info("wsurl %s", wsurl.c_str());

    gS->connectRun(wsurl);
    gS->wrtc_signal_thread_.Join();
    gS->wrtc_work_thread_.Join();

    Info("quit");

    return 0;
}
