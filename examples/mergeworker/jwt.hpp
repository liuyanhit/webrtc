#ifndef JWT_H_
#define JWT_H_

#include "rtc_base/json.h"

int JwtDecode(std::string in, Json::Value& out);

#endif
