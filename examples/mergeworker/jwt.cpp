#include "jwt.hpp"
#include "base64.h"

int JwtDecode(std::string in_jsw, Json::Value& out_json) {
    size_t num_header = 0, num_payload = 0, num_signature = 0;
    size_t num_jws_token = in_jsw.size();
    const char *jws_token = in_jsw.c_str(), *payload = jws_token,
               *signature = jws_token, *it = jws_token;
    int idx = 0;
    for (; it < (jws_token + num_jws_token) && idx < 3; it++) {
        if (*it == '.') {
            idx++;
            if (idx == 1) {
                // Found the first .
                num_header = (it - jws_token);
                payload = (it + 1);
            }
            if (idx == 2) {
                // Found the 2nd .
                num_payload = (it - payload);
                num_signature = num_jws_token - (it - jws_token) - 1;
                signature = it + 1;
            }
        } else if (!Base64Encode::IsValidBase64Char(*it)) {
            return -1;
        }
    }

    if (idx != 2) {
        return -2;
    }

    size_t num_dec_payload = Base64Encode::DecodeBytesNeeded(num_payload);
    str_ptr dec_payload(new char[num_dec_payload]);

    if (Base64Encode::DecodeUrl(payload, num_payload, dec_payload.get(),
                                &num_dec_payload) != 0) {
        return -3;
    }

    dec_payload.get()[num_dec_payload] = 0;

    Json::Reader r;
    r.parse(dec_payload.get(), dec_payload.get()+num_dec_payload, out_json, false);

    return 0;
}