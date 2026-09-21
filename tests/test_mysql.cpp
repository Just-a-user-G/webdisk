#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "db/mysql.h"

int main() {
    // 1. 读配置
    std::ifstream ifs("config/config.json");
    if (!ifs.is_open()) {
        std::cerr << "cannot open config/config.json\n";
        return 1;
    }
    nlohmann::json cfg;
    ifs >> cfg;

    // 2. 初始化
    MySQL::init(
        cfg["mysql"]["host"].get<std::string>(),
        cfg["mysql"]["user"].get<std::string>(),
        cfg["mysql"]["password"].get<std::string>(),
        cfg["mysql"]["database"].get<std::string>()
    );

    // 3. 连库并查询
    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement("SELECT COUNT(*) AS c FROM tbl_user")
        );
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (rs->next()) {
            std::cout << "connect OK, tbl_user count = " << rs->getInt("c") << "\n";
        }
    } catch (sql::SQLException& e) {
        std::cerr << "SQL error: " << e.what() << "\n";
        std::cerr << "error code: " << e.getErrorCode() << "\n";
        return 1;
    }

    return 0;
}
