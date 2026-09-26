#pragma once
#include <wfrest/HttpServer.h>

namespace gateway {

// 用户模块
void signup(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void signin(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void info  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);

// 文件模块
void fileQuery   (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void fileUpload  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void uploadInit  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void uploadChunk (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void uploadComplete(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void uploadStatus(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void fileDownload(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void fileDelete  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);
void fileRename  (const wfrest::HttpReq* req, wfrest::HttpResp* resp);

} // namespace gateway
