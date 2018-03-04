#include "msgpump.hpp"
#include <stdio.h>
#include <stdint.h>

MsgPump::MsgPump(MsgPumpReadMessageCb on_msg) {
    on_msg_ = on_msg;
}

int MsgPump::WriteMessage(const std::string& type, const Json::Value& message) {
    std::lock_guard<std::mutex> lock(wlock_);

    const std::string ms = rtc::JsonValueToString(message);
    Verbose("WriteCmd %s=%s", type.c_str(), ms.c_str());

    uint8_t a[4];
    uint32_t len = type.size()+1+ms.size();
    a[0] = (len>>24)&0xff;
    a[1] = (len>>16)&0xff;
    a[2] = (len>>8)&0xff;
    a[3] = len&0xff;


    if (fwrite(a, 1, sizeof(a), stdout) != sizeof(a)) {
        return -1;
    }
    if (fwrite(type.c_str(), 1, type.size(), stdout) != type.size()) {
        return -1;
    }
    if (fwrite("=", 1, 1, stdout) != 1) {
        return -1;
    }
    if (fwrite(ms.c_str(), 1, ms.size(), stdout) != ms.size()) {
        return -1;
    }

    return 0;
}

int MsgPump::readMessage(std::string& type, Json::Value& message) {
    uint8_t a[4];

    int fr = fread(a, 1, sizeof(a), stdin);
    if (fr != sizeof(a)) {
        return -1;
    }

    uint32_t len = (a[0]<<24)|(a[1]<<16)|(a[2]<<8)|a[3];
    char *data = (char *)malloc(len);
    int r = 0;
    int eq = -1;
    Json::Reader jr;

    fr = fread(data, 1, len, stdin);
    if (fr != (int)len) {
        goto out_free;
    }

    for (int i = 0; i < (int)len; i++) {
        if (data[i] == '=') {
            eq = i;
            break;
        }        
    }

    if (eq == -1) {
        r = -1;
        goto out_free;
    }

    type = std::string(data, eq);
    if (!jr.parse(data+eq+1, data+len, message, false)) {
        r = -1;
        goto out_free;
    }

out_free:
    free(data);
    return r;
}

void MsgPump::Run() {
    for (;;) {
        std::string type;
        Json::Value message;
        int r = readMessage(type, message);
        if (r < 0)
            break;
        if (on_msg_) {
            on_msg_(type, message);
        }
    }
}