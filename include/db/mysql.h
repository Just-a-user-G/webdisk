#pragma once
#include <memory>
#include <string>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

class MySQL {
public:
    // 保存连接信息，程序启动时调用一次
    static void init(const std::string& host, const std::string& user,
                     const std::string& password, const std::string& database);

    // 每次需要操作数据库时调用，返回一个新建的连接
    static std::unique_ptr<sql::Connection> getConnection();

private:
    static std::string host_, user_, password_, database_;
};
