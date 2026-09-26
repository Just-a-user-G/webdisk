#pragma once
#include "user.srpc.h"
#include <string>

// 文件服务调用用户服务的客户端封装
class UserClient {
public:
    static UserClient& instance();

    // 初始化，连接用户服务
    bool init(const std::string& host, unsigned short port);

    // 校验 Token，成功返回 true 并写 uid
    bool checkToken(const std::string& username,
                    const std::string& token,
                    int& uid);

private:
    UserClient() = default;
    UserClient(const UserClient&) = delete;
    UserClient& operator=(const UserClient&) = delete;

    std::unique_ptr<webdisk::UserService::SRPCClient> client_;
};
