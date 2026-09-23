#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <csignal>
#include <chrono>
#include <thread>

using json = nlohmann::json;

static bool g_running = true;

void onSignal(int) {
    g_running = false;
}

// 把 src_path 的内容备份到 dst_path
bool backupFile(const std::string& src_path, const std::string& dst_path) {
    std::ifstream src(src_path, std::ios::binary);
    if (!src.is_open()) return false;

    std::ofstream dst(dst_path, std::ios::binary);
    if (!dst.is_open()) return false;

    dst << src.rdbuf();
    return dst.good();
}

int main() {
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    const std::string uri          = "amqp://guest:guest@127.0.0.1:5672/%2f";
    const std::string queue        = "backup.queue";
    const std::string upload_dir   = "uploads/";
    const std::string backup_dir   = "backup/";

    try {
        auto channel = AmqpClient::Channel::CreateFromUri(uri);

        // 声明死信队列
        std::string dlq = queue + ".dlq";
        channel->DeclareQueue(dlq, false, true, false, false);

        // 声明主队列：必须和生产者端参数完全一致，否则 406
        AmqpClient::Table args;
        args["x-dead-letter-exchange"]    = AmqpClient::TableValue("");
        args["x-dead-letter-routing-key"] = AmqpClient::TableValue(dlq);
        channel->DeclareQueue(queue, false, true, false, false, args);

        std::cout << "backup_worker started, polling queue: " << queue << "\n";

        while (g_running) {
            AmqpClient::Envelope::ptr_t env;
            // no_ack = false：手动 ack
            bool got = channel->BasicGet(env, queue, false);

            if (!got || !env || !env->Message()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            std::string body = env->Message()->Body();
            std::cout << "received: " << body << std::endl;

            // 1. 解析 JSON
            std::string hashcode, filename;
            int uid = 0;
            long long size = 0;
            try {
                json j = json::parse(body);
                hashcode = j.at("hashcode").get<std::string>();
                filename = j.at("filename").get<std::string>();
                uid      = j.at("uid").get<int>();
                size     = j.at("size").get<long long>();
            } catch (std::exception& e) {
                std::cerr << "  parse failed: " << e.what() << ", discard\n";
                channel->BasicAck(env);   // 格式错的直接丢掉
                continue;
            }

            // 2. 执行备份
            std::string src = upload_dir + hashcode;
            std::string dst = backup_dir + hashcode;

            if (backupFile(src, dst)) {
                std::cout << "  backed up: " << filename
                          << " (" << size << " bytes) -> " << dst << std::endl;
                channel->BasicAck(env);
            } else {
                std::cerr << "  backup failed for " << hashcode
                          << ", send to DLQ\n";
                // requeue = false，不重入队 → 自动进死信队列
                channel->BasicReject(env, false);
            }
        }

        std::cout << "\nshutting down gracefully...\n";
        std::cout << "backup_worker stopped\n";
    } catch (std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}