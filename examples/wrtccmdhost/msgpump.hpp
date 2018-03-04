#ifndef __MSGPUMP_HPP__
#define __MSGPUMP_HPP__

#include <stdio.h>
#include <string>
#include "rtc_base/json.h"
#include "common.hpp"

typedef std::function<void (const std::string& type, const Json::Value& message)> MsgPumpReadMessageCb;

class MsgPump {
public:
    MsgPump(MsgPumpReadMessageCb onMsg);
    int WriteMessage(const std::string& type, const Json::Value& message);
    void Run();

private:
    MsgPumpReadMessageCb on_msg_;
    std::mutex wlock_;

    int readMessage(std::string& type, Json::Value& message);
};

#endif