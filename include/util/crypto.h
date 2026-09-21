#pragma once
#include <string>

namespace crypto {
    // 对输入做 SHA256，返回十六进制字符串
    std::string sha256_hex(const std::string& data);

    // 生成指定字节数的随机数据，返回十六进制字符串
    std::string random_hex(size_t bytes);

    // 增量 SHA256：适合大文件边读边算
    class Sha256Hasher {
    public:
        Sha256Hasher();
        ~Sha256Hasher();
        Sha256Hasher(const Sha256Hasher&) = delete;
        Sha256Hasher& operator=(const Sha256Hasher&) = delete;

        void update(const void* data, size_t len);
        std::string final_hex();   // 调用后不应再 update

    private:
        void* ctx_;
    };
}
