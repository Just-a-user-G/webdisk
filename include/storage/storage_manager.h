#pragma once
#include "storage/storage.h"
#include <string>

// 全局存储管理器：程序启动时初始化，之后通过 get() 使用
namespace storage_manager {

    // 根据 type 创建对应的 Storage 实现
    // type: "local" 或 "oss"
    bool init(const std::string& type, const std::string& upload_dir);

    // 返回全局 Storage 指针，未初始化时返回 nullptr
    Storage* get();

}
