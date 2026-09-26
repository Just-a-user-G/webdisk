#include "common/util/auth.h"
#include "common/db/mysql.h"
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

namespace auth {

bool checkToken(const std::string& username, const std::string& token, int& uid) {
    auto conn = MySQL::getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(
        conn->prepareStatement(
            "SELECT u.id FROM tbl_user u "
            "JOIN tbl_user_token t ON u.id = t.uid "
            "WHERE u.username = ? AND t.token = ? "
            "AND t.expired_at > NOW() AND u.tomb = 0"
        )
    );
    ps->setString(1, username);
    ps->setString(2, token);
    std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
    if (rs->next()) {
        uid = rs->getInt("id");
        return true;
    }
    return false;
}

}
