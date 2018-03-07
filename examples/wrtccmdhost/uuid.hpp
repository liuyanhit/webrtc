#ifndef _UUID_H_
#define _UUID_H_

#include <random>

static inline std::string newUUID() {
    std::random_device rd;
    const char *dig = "0123456789abcdef";
    char buf[12];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = dig[rd()%16];
    }
    return std::string(buf, sizeof(buf));
}

#endif