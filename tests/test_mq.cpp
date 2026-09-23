#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <iostream>
#include <string>

int main() {
    using namespace AmqpClient;
    using std::string;

    try {
        // 1. 连接
        string uri = "amqp://guest:guest@127.0.0.1:5672/%2f";
        Channel::ptr_t channel = Channel::CreateFromUri(uri);
        std::cout << "Connected to RabbitMQ\n";

        // 2. 声明队列
        string queue_name = "test.queue";
        channel->DeclareQueue(queue_name, false, true, false, false);

        // 3. 发一条消息
        string body = "Hello RabbitMQ";
        BasicMessage::ptr_t msg = BasicMessage::Create(body);
        channel->BasicPublish("", queue_name, msg);
        std::cout << "Published: " << body << "\n";

        // 4. 收一条消息
        channel->BasicConsume(queue_name, "", true, false, false, 1);
        Envelope::ptr_t env = channel->BasicConsumeMessage();
        std::cout << "Received: " << env->Message()->Body() << "\n";

        std::cout << "OK\n";
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
