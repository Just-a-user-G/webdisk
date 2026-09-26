#include "common/storage/local_storage.h"
#include <fstream>
#include <iterator>
#include <cstdio>

LocalStorage::LocalStorage(const std::string& base_dir) : base_dir_(base_dir) {
    // 确保目录以 / 结尾
    if (!base_dir_.empty() && base_dir_.back() != '/')
        base_dir_ += '/';
}

std::string LocalStorage::pathOf(const std::string& key) const {
    return base_dir_ + key;
}

bool LocalStorage::put(const std::string& key, const std::string& content) {
    std::ofstream ofs(pathOf(key), std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(content.data(), content.size());
    return ofs.good();
}

bool LocalStorage::get(const std::string& key, std::string& out) {
    std::ifstream ifs(pathOf(key), std::ios::binary);
    if (!ifs.is_open()) return false;
    out.assign((std::istreambuf_iterator<char>(ifs)),
                std::istreambuf_iterator<char>());
    return true;
}

bool LocalStorage::exists(const std::string& key) {
    std::ifstream ifs(pathOf(key), std::ios::binary);
    return ifs.is_open();
}

bool LocalStorage::remove(const std::string& key) {
    return std::remove(pathOf(key).c_str()) == 0;
}

bool LocalStorage::putFromFile(const std::string& key, const std::string& src_path) {
    // 先尝试原子 rename（同文件系统内）
    if (std::rename(src_path.c_str(), pathOf(key).c_str()) == 0)
        return true;

    // 跨文件系统时退化为拷贝 + 删除
    std::ifstream src(src_path, std::ios::binary);
    if (!src.is_open()) return false;
    std::ofstream dst(pathOf(key), std::ios::binary);
    if (!dst.is_open()) return false;

    dst << src.rdbuf();
    bool ok = dst.good();

    src.close();
    dst.close();
    std::remove(src_path.c_str());
    return ok;
}