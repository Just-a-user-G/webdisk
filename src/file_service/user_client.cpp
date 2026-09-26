#include "file_service/user_client.h"
#include <iostream>

UserClient& UserClient::instance() {
    static UserClient inst;
    return inst;
}

bool UserClient::init(const std::string& host, unsigned short port) {
    try {
        client_ = std::make_unique<webdisk::UserService::SRPCClient>(host.c_str(), port);
        std::cout << "UserClient connected to " << host << ":" << port << "\n";
        return true;
    } catch (std::exception& e) {
        std::cerr << "UserClient init failed: " << e.what() << "\n";
        return false;
    }
}

bool UserClient::checkToken(const std::string& username,
                            const std::string& token,
                            int& uid) {
    webdisk::CheckTokenReq req;
    req.set_username(username);
    req.set_token(token);

    webdisk::CheckTokenResp resp;
    srpc::RPCSyncContext ctx;

    client_->CheckToken(&req, &resp, &ctx);

    if (!ctx.success) {
        std::cerr << "CheckToken RPC failed: " << ctx.errmsg << "\n";
        return false;
    }

    if (resp.code() != 0) return false;

    uid = resp.uid();
    return true;
}
