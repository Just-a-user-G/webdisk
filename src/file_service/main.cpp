#include "file.srpc.h"
#include "file_service/file_service_impl.h"
#include "file_service/user_client.h"
#include "workflow/WFFacilities.h"
#include "common/db/mysql.h"
#include "common/storage/storage_manager.h"
#include "common/mq/producer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>

using namespace srpc;

static WFFacilities::WaitGroup g_wait_group(1);

void onSignal(int) {
    g_wait_group.done();
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // 1. 加载配置
    std::ifstream ifs("config/config.json");
    if (!ifs.is_open()) {
        std::cerr << "cannot open config/config.json\n";
        return 1;
    }
    nlohmann::json cfg;
    ifs >> cfg;

    // 2. 初始化 MySQL
    MySQL::init(
        cfg["mysql"]["host"].get<std::string>(),
        cfg["mysql"]["user"].get<std::string>(),
        cfg["mysql"]["password"].get<std::string>(),
        cfg["mysql"]["database"].get<std::string>()
    );

    // 3. 初始化 Storage
    if (!storage_manager::init(
            cfg["storage"]["type"].get<std::string>(),
            cfg["storage"]["upload_dir"].get<std::string>())) {
        std::cerr << "Failed to init storage\n";
        return 1;
    }

    // 4. 初始化 MQ 生产者
    if (!Producer::instance().init(
            cfg["mq"]["uri"].get<std::string>(),
            cfg["mq"]["exchange"].get<std::string>(),
            cfg["mq"]["routing_key"].get<std::string>(),
            cfg["mq"]["queue"].get<std::string>())) {
        std::cerr << "Failed to init MQ producer\n";
        return 1;
    }

    // 5. 初始化用户服务客户端（用于校验 Token）
    if (!UserClient::instance().init("127.0.0.1", 9001)) {
        std::cerr << "Failed to connect to user_service\n";
        return 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    
    // 6. 启动 RPC 服务器
    unsigned short port = 9002;
    SRPCServer server;

    FileServiceImpl impl;
    server.add_service(&impl);

    if (server.start(port) != 0) {
        std::cerr << "file_service: cannot start on port " << port << "\n";
        return 1;
    }
    std::cout << "file_service started on port " << port << "\n";

    g_wait_group.wait();

    server.stop();
    std::cout << "file_service stopped\n";

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
