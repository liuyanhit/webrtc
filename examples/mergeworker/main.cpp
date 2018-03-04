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

static std::string getUrlFromToken(const Json::Value& v) {
    std::string addr = v["roomserveraddr"].asString();
    return "ws://"+addr+"/signaling";
}

class PosManager {
public:
    std::mutex l_;
    std::deque<std::string> names_;
    muxer::AvMuxer *m;

    double WindowHeight = 1280;
    double WindowWidth = 720;

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

    std::function<void (const std::string& name, int x, int y, int w, int h, int z)> OnUpdate;

    void update(int idx, const std::string& name) {
        int x, y, w, h, z;
        getWindowPos(idx, x, y, w, h, z);
        Info("PosManagerUIUpdate %d %s (%d,%d) %dx%d %d", idx, name.c_str(), x,y,w,h,z);
        if (OnUpdate)
            OnUpdate(name, x, y, w, h, z);
    }

    void updateAll() {
        for (int i = 0; i < (int)names_.size(); i++) {
            update(i, names_[i]);
        }
    }

    void Add(const std::string& name) {
        std::lock_guard<std::mutex> lock(l_);
        names_.push_back(name);
        updateAll();
    }

    void Remove(const std::string& name) {
        std::lock_guard<std::mutex> lock(l_);
        for (auto it = names_.begin(); it != names_.end(); it++) {
            if (*it == name) {
                    it = names_.erase(it);
                    break;
            }
        }
        updateAll();
    }
};

int main(int argc, char **argv) {
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
            Fatal("ParseTokenFailed");
        }

        Info("PublishUrl %s", publishUrl.c_str());
    }

    auto posman = new PosManager();
    auto m = std::make_unique<muxer::AvMuxer>(posman->WindowWidth, posman->WindowHeight);

    posman->OnUpdate = [&m](const std::string& name, int x, int y, int w, int h, int z) {
        auto input = m->FindInput(name);
        if (input == nullptr)
            return;
        input->SetOption("x", x);
        input->SetOption("y", y);
        input->SetOption("w", w);
        input->SetOption("h", h);
        input->SetOption("z", z);
    };

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

    Info("WsURL %s", wsurl.c_str());

    gS->connectRun(wsurl);
    gS->wrtc_signal_thread_.Join();
    gS->wrtc_work_thread_.Join();

    Info("Quit");

    return 0;
}
