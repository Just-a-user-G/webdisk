#include "gateway/rpc_clients.h"
#include <iostream>

namespace gateway {

RpcClients& RpcClients::instance() {
    static RpcClients inst;
    return inst;
}

bool RpcClients::init(const std::string& user_host, unsigned short user_port,
                      const std::string& file_host, unsigned short file_port) {
    try {
        user_ = std::make_unique<webdisk::UserService::SRPCClient>(user_host.c_str(), user_port);
        file_ = std::make_unique<webdisk::FileService::SRPCClient>(file_host.c_str(), file_port);
        // 预热：发一次空请求，把 TCP 连接建起来
        {
            webdisk::CheckTokenReq req;
            req.set_username("");
            req.set_token("");
            webdisk::CheckTokenResp resp;
            srpc::RPCSyncContext ctx;
            user_->CheckToken(&req, &resp, &ctx);
        }
        {
            webdisk::FileQueryReq req;
            req.set_username("");
            req.set_token("");
            webdisk::FileQueryResp resp;
            srpc::RPCSyncContext ctx;
            file_->FileQuery(&req, &resp, &ctx);
        }
        std::cout << "RPC clients connected: user "
                  << user_host << ":" << user_port
                  << ", file " << file_host << ":" << file_port << "\n";
        return true;
    } catch (std::exception& e) {
        std::cerr << "RPC clients init failed: " << e.what() << "\n";
        return false;
    }
}

webdisk::UserService::SRPCClient* RpcClients::user() { return user_.get(); }
webdisk::FileService::SRPCClient* RpcClients::file() { return file_.get(); }

} // namespace gateway
