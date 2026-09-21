#pragma once
#include <wfrest/HttpServer.h>

namespace handler {
    void signup(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
    void signin(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
    void info  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
}
