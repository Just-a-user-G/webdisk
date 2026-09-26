#pragma once
#include "common/storage/storage.h"
#include <string>

// 本地文件系统存储：文件保存到 <base_dir>/<key>
class LocalStorage : public Storage {
public:
    explicit LocalStorage(const std::string& base_dir);

    bool put(const std::string& key, const std::string& content) override;
    bool get(const std::string& key, std::string& out) override;
    bool exists(const std::string& key) override;
    bool remove(const std::string& key) override;
    bool putFromFile(const std::string& key, const std::string& src_path) override;

private:
    std::string base_dir_;
    std::string pathOf(const std::string& key) const;
};
