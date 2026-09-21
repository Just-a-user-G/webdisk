#pragma once
#include <string>

// 存储抽象接口：屏蔽本地文件系统和云存储的差异
class Storage {
public:
    virtual ~Storage() = default;

    // 保存文件，key 是唯一标识（这里是 hashcode），content 是文件内容
    virtual bool put(const std::string& key, const std::string& content) = 0;

    // 读取文件，成功返回 true 并写入 out
    virtual bool get(const std::string& key, std::string& out) = 0;

    // 判断文件是否存在
    virtual bool exists(const std::string& key) = 0;

    // 删除文件
    virtual bool remove(const std::string& key) = 0;

    // 从本地文件保存（适合大文件，避免把内容读进内存）
    // src_path 是调用方准备好的临时文件，成功后调用方不应再用这个文件
    virtual bool putFromFile(const std::string& key, const std::string& src_path) = 0;
};
