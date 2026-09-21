#pragma once
#include <string>

namespace auth {
    // 校验 username + token 是否有效
    // 有效则把 uid 写入 out 参数并返回 true
    // 无效返回 false
    bool checkToken(const std::string& username, const std::string& token, int& uid);
}
