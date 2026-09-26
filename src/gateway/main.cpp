#include <wfrest/HttpServer.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>

#include "gateway/gateway_handler.h"
#include "gateway/rpc_clients.h"

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

    // 2. 初始化 RPC 客户端
    if (!gateway::RpcClients::instance().init(
            "127.0.0.1", 9001,   // user_service
            "127.0.0.1", 9002)) { // file_service
        std::cerr << "Failed to init RPC clients\n";
        return 1;
    }

    // 3. 启动 HTTP 服务器
    wfrest::HttpServer server;
    g_server = &server;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    server.GET("/ping", [](const wfrest::HttpReq*, wfrest::HttpResp* resp) {
        resp->String("pong\n");
    });

    // 用户模块
    server.POST("/user/signup", gateway::signup);
    server.POST("/user/signin", gateway::signin);
    server.GET ("/user/info",   gateway::info);

    // 文件模块
    server.POST("/file/query",          gateway::fileQuery);
    server.POST("/file/upload",         gateway::fileUpload);
    server.POST("/file/upload/init",    gateway::uploadInit);
    server.POST("/file/upload/chunk",   gateway::uploadChunk);
    server.POST("/file/upload/complete",gateway::uploadComplete);
    server.GET ("/file/upload/status",  gateway::uploadStatus);
    server.GET ("/file/download",       gateway::fileDownload);
    server.POST("/file/delete",         gateway::fileDelete);
    server.POST("/file/rename",         gateway::fileRename);

    // 静态文件
    server.Static("/static", "static");

    int port = cfg["server"]["port"].get<int>();
    if (server.start(port) == 0) {
        std::cout << "gateway started on http://127.0.0.1:" << port << "\n";
        getchar();
        server.stop();
    } else {
        std::cerr << "Cannot start gateway\n";
        return 1;
    }
    return 0;
}
