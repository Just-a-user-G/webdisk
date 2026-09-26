#include "common/storage/storage_manager.h"
#include "common/storage/local_storage.h"
#include <memory>
#include <iostream>

namespace {
    std::unique_ptr<Storage> g_storage;
}

namespace storage_manager {

bool init(const std::string& type, const std::string& upload_dir) {
    if (type == "local") {
        g_storage = std::make_unique<LocalStorage>(upload_dir);
        std::cout << "Storage: local (" << upload_dir << ")\n";
        return true;
    }
    if (type == "oss") {
        // TODO: 等有阿里云账号后实现 OssStorage
        std::cerr << "Storage: oss not implemented yet\n";
        return false;
    }
    std::cerr << "Storage: unknown type: " << type << "\n";
    return false;
}

Storage* get() {
    return g_storage.get();
}

}
