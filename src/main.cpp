#include <wfrest/HttpServer.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>

#include "db/mysql.h"
#include "handler/user_handler.h"
#include "handler/file_handler.h"
#include "storage/storage_manager.h"
#include "mq/producer.h"

static wfrest::HttpServer* g_server = nullptr;

void onSignal(int) {
    if (g_server) g_server->stop();
}

int main() {
    // 1. 加载配置
    std::ifstream ifs("config/config.json");
    if (!ifs.is_open()) {
        std::cerr << "cannot open config/config.json\n";
        return 1;
    }
    nlohmann::json cfg;
    ifs >> cfg;

    // 2. 初始化 MySQL 连接信息
    MySQL::init(
        cfg["mysql"]["host"].get<std::string>(),
        cfg["mysql"]["user"].get<std::string>(),
        cfg["mysql"]["password"].get<std::string>(),
        cfg["mysql"]["database"].get<std::string>()
    );

    // 2.5 初始化 MQ 生产者
    if (!Producer::instance().init(
            cfg["mq"]["uri"].get<std::string>(),
            cfg["mq"]["exchange"].get<std::string>(),
            cfg["mq"]["routing_key"].get<std::string>(),
            cfg["mq"]["queue"].get<std::string>())) {
        std::cerr << "Failed to init MQ producer\n";
        return 1;
    }

    // 初始化 Storage
    if (!storage_manager::init(
            cfg["storage"]["type"].get<std::string>(),
            cfg["storage"]["upload_dir"].get<std::string>())) {
        std::cerr << "Failed to init storage\n";
        return 1;
    }

    // 3. 启动 HTTP 服务
    wfrest::HttpServer server;
    g_server = &server;
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    // 健康检查
    server.GET("/ping", [](const wfrest::HttpReq*, wfrest::HttpResp* resp) {
        resp->String("pong\n");
    });

    // 用户模块路由
    server.POST("/user/signup", handler::signup);
    server.POST("/user/signin", handler::signin);
    server.GET ("/user/info",   handler::info);

    // 文件模块路由
    server.POST("/file/query", handler::fileQuery);
    server.POST("/file/upload", handler::fileUpload);
    server.GET ("/file/download", handler::fileDownload);
    server.POST("/file/delete", handler::fileDelete);
    server.POST("/file/rename", handler::fileRename);
    server.POST("/file/upload/init", handler::uploadInit);
    server.POST("/file/upload/chunk", handler::uploadChunk);
    server.POST("/file/upload/complete", handler::uploadComplete);
    server.GET ("/file/upload/status",  handler::uploadStatus);

    // 静态文件服务
    server.Static("/static", "static");

    int port = cfg["server"]["port"].get<int>();
    if (server.start(port) == 0) {
        std::cout << "Server started on http://127.0.0.1:" << port << "\n";
        getchar();
        server.stop();
    } else {
        std::cerr << "Cannot start server\n";
        return 1;
    }
    return 0;
}
