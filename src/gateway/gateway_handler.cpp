#include "gateway/gateway_handler.h"
#include "gateway/rpc_clients.h"
#include "user.srpc.h"
#include "file.srpc.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cctype>

using json = nlohmann::json;
using namespace gateway;

namespace {

// 从 URL 查询串取参数
std::string queryOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    return req->query(key);
}

// 从 x-www-form-urlencoded 表单取参数
std::string formOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    const auto& kv = req->form_kv();
    auto it = kv.find(key);
    return (it != kv.end()) ? it->second : std::string();
}

// 统一返回
void replyJson(wfrest::HttpResp* resp, int code, const std::string& msg,
               const json& data = json()) {
    json j;
    j["code"] = code;
    j["msg"]  = msg;
    if (!data.is_null()) j["data"] = data;
    resp->String(j.dump());
}

// 根据扩展名推断 MIME
std::string guessMimeType(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = filename.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(c);

    if (ext == "txt")  return "text/plain; charset=utf-8";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")  return "text/css";
    if (ext == "js")   return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "pdf")  return "application/pdf";
    if (ext == "zip")  return "application/zip";
    return "application/octet-stream";
}

} // anonymous namespace

namespace gateway {

// ==================== 用户模块 ====================

void signup(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = formOrEmpty(req, "username");
    std::string password = formOrEmpty(req, "password");

    webdisk::SignupReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_password(password);

    webdisk::SignupResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().user()->Signup(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    replyJson(resp, rpc_resp.code(), rpc_resp.msg());
}

void signin(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = formOrEmpty(req, "username");
    std::string password = formOrEmpty(req, "password");

    webdisk::SigninReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_password(password);

    webdisk::SigninResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().user()->Signin(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["Username"] = rpc_resp.username();
    data["Token"]    = rpc_resp.token();
    data["Location"] = "/static/view/home.html";
    replyJson(resp, 0, "SUCCESS", data);
}

void info(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    webdisk::GetUserInfoReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);

    webdisk::GetUserInfoResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().user()->GetUserInfo(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["Username"] = rpc_resp.username();
    data["SignupAt"] = rpc_resp.signup_at();
    replyJson(resp, 0, "SUCCESS", data);
}

// ==================== 文件模块 ====================

void fileQuery(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    webdisk::FileQueryReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);

    std::string limit_str = formOrEmpty(req, "limit");
    if (!limit_str.empty()) {
        try { rpc_req.set_limit(std::stoi(limit_str)); } catch (...) {}
    }
    std::string offset_str = formOrEmpty(req, "offset");
    if (!offset_str.empty()) {
        try { rpc_req.set_offset(std::stoi(offset_str)); } catch (...) {}
    }
    rpc_req.set_keyword(formOrEmpty(req, "keyword"));

    webdisk::FileQueryResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->FileQuery(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json arr = json::array();
    for (int i = 0; i < rpc_resp.items_size(); ++i) {
        const auto& item = rpc_resp.items(i);
        json j;
        j["FileHash"]    = item.file_hash();
        j["FileName"]    = item.file_name();
        j["FileSize"]    = item.file_size();
        j["UploadAt"]    = item.upload_at();
        j["LastUpdated"] = item.last_updated();
        arr.push_back(j);
    }
    replyJson(resp, 0, "SUCCESS", arr);
}

void fileUpload(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    wfrest::Form& form = req->form();
    auto it = form.find("file");
    if (it == form.end()) return replyJson(resp, 1, "no file field");

    std::string filename = it->second.first;
    std::string content  = it->second.second;

    webdisk::FileUploadReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_filename(filename);
    rpc_req.set_content(content);

    webdisk::FileUploadResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->FileUpload(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["FileHash"] = rpc_resp.file_hash();
    data["FileName"] = rpc_resp.file_name();
    data["FileSize"] = rpc_resp.file_size();
    replyJson(resp, 0, "SUCCESS", data);
}

void uploadInit(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    webdisk::UploadInitReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_filename(formOrEmpty(req, "filename"));

    std::string tc = formOrEmpty(req, "total_chunks");
    if (!tc.empty()) { try { rpc_req.set_total_chunks(std::stoi(tc)); } catch (...) {} }
    std::string ts = formOrEmpty(req, "total_size");
    if (!ts.empty()) { try { rpc_req.set_total_size(std::stoll(ts)); } catch (...) {} }

    webdisk::UploadInitResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->UploadInit(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["UploadID"]    = rpc_resp.upload_id();
    data["TotalChunks"] = rpc_resp.total_chunks();
    data["TotalSize"]   = rpc_resp.total_size();
    replyJson(resp, 0, "SUCCESS", data);
}

void uploadChunk(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    std::string upload_id = queryOrEmpty(req, "upload_id");
    std::string chunk_index_str = queryOrEmpty(req, "chunk_index");

    wfrest::Form& form = req->form();
    auto it = form.find("chunk");
    if (it == form.end()) return replyJson(resp, 1, "no chunk field");

    webdisk::UploadChunkReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_upload_id(upload_id);
    try { rpc_req.set_chunk_index(std::stoi(chunk_index_str)); } catch (...) {
        return replyJson(resp, 1, "invalid chunk_index");
    }
    rpc_req.set_chunk_data(it->second.second);

    webdisk::UploadChunkResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->UploadChunk(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["UploadID"]   = rpc_resp.upload_id();
    data["ChunkIndex"] = rpc_resp.chunk_index();
    data["ChunkSize"]  = rpc_resp.chunk_size();
    replyJson(resp, 0, "SUCCESS", data);
}

void uploadComplete(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username  = queryOrEmpty(req, "username");
    std::string token     = queryOrEmpty(req, "token");
    std::string upload_id = queryOrEmpty(req, "upload_id");

    webdisk::UploadCompleteReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_upload_id(upload_id);

    webdisk::UploadCompleteResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->UploadComplete(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json data;
    data["FileHash"] = rpc_resp.file_hash();
    data["FileName"] = rpc_resp.file_name();
    data["FileSize"] = rpc_resp.file_size();
    replyJson(resp, 0, "SUCCESS", data);
}

void uploadStatus(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username  = queryOrEmpty(req, "username");
    std::string token     = queryOrEmpty(req, "token");
    std::string upload_id = queryOrEmpty(req, "upload_id");

    webdisk::UploadStatusReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_upload_id(upload_id);

    webdisk::UploadStatusResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->UploadStatus(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    json received = json::array();
    for (int i = 0; i < rpc_resp.received_chunks_size(); ++i)
        received.push_back(rpc_resp.received_chunks(i));

    json data;
    data["UploadID"]       = rpc_resp.upload_id();
    data["FileName"]       = rpc_resp.file_name();
    data["TotalChunks"]    = rpc_resp.total_chunks();
    data["ReceivedChunks"] = received;
    replyJson(resp, 0, "SUCCESS", data);
}

void fileDownload(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    std::string filename = queryOrEmpty(req, "filename");
    std::string filehash = queryOrEmpty(req, "filehash");

    webdisk::FileDownloadReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_filename(filename);
    rpc_req.set_file_hash(filehash);

    webdisk::FileDownloadResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->FileDownload(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    if (rpc_resp.code() != 0) return replyJson(resp, rpc_resp.code(), rpc_resp.msg());

    resp->headers["Content-Disposition"] =
        "attachment; filename=\"" + rpc_resp.filename() + "\"";
    resp->headers["Content-Type"] = guessMimeType(rpc_resp.filename());
    resp->String(rpc_resp.content());
}

void fileDelete(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    webdisk::FileDeleteReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_filename(formOrEmpty(req, "filename"));
    rpc_req.set_file_hash(formOrEmpty(req, "filehash"));

    webdisk::FileDeleteResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->FileDelete(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    replyJson(resp, rpc_resp.code(), rpc_resp.msg());
}

void fileRename(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");

    webdisk::FileRenameReq rpc_req;
    rpc_req.set_username(username);
    rpc_req.set_token(token);
    rpc_req.set_old_filename(formOrEmpty(req, "old_filename"));
    rpc_req.set_new_filename(formOrEmpty(req, "new_filename"));
    rpc_req.set_file_hash(formOrEmpty(req, "filehash"));

    webdisk::FileRenameResp rpc_resp;
    srpc::RPCSyncContext ctx;
    RpcClients::instance().file()->FileRename(&rpc_req, &rpc_resp, &ctx);

    if (!ctx.success) return replyJson(resp, 1, "RPC failed: " + ctx.errmsg);
    replyJson(resp, rpc_resp.code(), rpc_resp.msg());
}

} // namespace gateway
