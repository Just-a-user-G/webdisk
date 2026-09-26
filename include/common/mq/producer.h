#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <SimpleAmqpClient/SimpleAmqpClient.h>

// RabbitMQ 生产者（单例）
class Producer {
public:
    static Producer& instance();

    // 初始化连接。程序启动时调用一次
    bool init(const std::string& uri,
              const std::string& exchange,
              const std::string& routing_key,
              const std::string& queue);

    // 发一条消息，成功返回 true
    bool publish(const std::string& body);

private:
    Producer() = default;
    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;

    std::string exchange_;
    std::string routing_key_;
    std::string queue_;
    AmqpClient::Channel::ptr_t channel_;
    std::mutex mutex_;
};
