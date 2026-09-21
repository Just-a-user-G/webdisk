#include "db/mysql.h"

std::string MySQL::host_, MySQL::user_, MySQL::password_, MySQL::database_;

void MySQL::init(const std::string& host, const std::string& user,
                 const std::string& password, const std::string& database) {
    host_ = host;
    user_ = user;
    password_ = password;
    database_ = database;
}

std::unique_ptr<sql::Connection> MySQL::getConnection() {
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
    std::unique_ptr<sql::Connection> conn(driver->connect(host_, user_, password_));
    conn->setSchema(database_);
    return conn;
}
