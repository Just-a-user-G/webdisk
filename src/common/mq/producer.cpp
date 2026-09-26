#include "common/mq/producer.h"
#include <iostream>
#include <stdexcept>

Producer& Producer::instance() {
    static Producer inst;
    return inst;
}

bool Producer::init(const std::string& uri,
                    const std::string& exchange,
                    const std::string& routing_key,
                    const std::string& queue) {
    try {
        exchange_    = exchange;
        routing_key_ = routing_key;
        queue_       = queue;

        channel_ = AmqpClient::Channel::CreateFromUri(uri);

        // 声明死信队列
        std::string dlq = queue_ + ".dlq";
        channel_->DeclareQueue(dlq, false, true, false, false); // 参数：queue, passive, durable, exclusive, auto_delete

        // 声明主队列：附加死信参数
        AmqpClient::Table args;
        args["x-dead-letter-exchange"]    = AmqpClient::TableValue("");
        args["x-dead-letter-routing-key"] = AmqpClient::TableValue(dlq);

        channel_->DeclareQueue(queue_, false, true, false, false, args);

        std::cout << "MQ producer connected, queue=" << queue_ << "\n";
        return true;
    } catch (std::exception& e) {
        std::cerr << "MQ producer init failed: " << e.what() << "\n";
        return false;
    }
}

bool Producer::publish(const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto msg = AmqpClient::BasicMessage::Create(body);
        msg->DeliveryMode(AmqpClient::BasicMessage::dm_persistent);
        channel_->BasicPublish(exchange_, routing_key_, msg);
        return true;
    } catch (std::exception& e) {
        std::cerr << "MQ publish failed: " << e.what() << "\n";
        return false;
    }
}
