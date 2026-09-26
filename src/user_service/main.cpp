#include "user.srpc.h"
#include "user_service/user_service_impl.h"
#include "workflow/WFFacilities.h"
#include "common/db/mysql.h"
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

    signal(SIGINT, onSignal);//注册信号处理,信号SIGINT触发场景：终端Ctrl+C
    signal(SIGTERM, onSignal);//信号SIGTERM触发场景：kill <pid> 或 systemctl stop
    
    // 3. 启动 RPC 服务器
    unsigned short port = 9001;
    SRPCServer server;

    UserServiceImpl impl;
    server.add_service(&impl);

    if (server.start(port) != 0) {
        std::cerr << "user_service: cannot start on port " << port << "\n";
        return 1;
    }
    std::cout << "user_service started on port " << port << "\n";

    g_wait_group.wait();

    server.stop();
    std::cout << "user_service stopped\n";

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
