#pragma once
#include "user.srpc.h"
#include "file.srpc.h"
#include <memory>

// 网关持有的 RPC 客户端
namespace gateway {

class RpcClients {
public:
    static RpcClients& instance();

    bool init(const std::string& user_host, unsigned short user_port,
              const std::string& file_host, unsigned short file_port);

    webdisk::UserService::SRPCClient* user();
    webdisk::FileService::SRPCClient* file();

private:
    RpcClients() = default;
    RpcClients(const RpcClients&) = delete;
    RpcClients& operator=(const RpcClients&) = delete;

    std::unique_ptr<webdisk::UserService::SRPCClient> user_;
    std::unique_ptr<webdisk::FileService::SRPCClient> file_;
};

} // namespace gateway
