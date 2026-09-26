#include "handler/user_handler.h"
#include "common/db/mysql.h"
#include "common/util/crypto.h"
#include "common/util/auth.h"
#include <nlohmann/json.hpp>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

using json = nlohmann::json;

namespace {

// 统一的错误返回
void fail(wfrest::HttpResp* resp, const std::string& msg) {
    json j;
    j["code"] = 1;
    j["msg"] = msg;
    resp->String(j.dump());
}

// 统一成功返回
void ok(wfrest::HttpResp* resp, const nlohmann::json& data = nlohmann::json()) {
    json j;
    j["code"] = 0;
    j["msg"]  = "SUCCESS";
    if (!data.is_null()) j["data"] = data;
    resp->String(j.dump());
}

// 从 URL query 里取参数，不存在返回空字符串
std::string queryOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    return req->query(key);   // 直接返回 std::string
}

// 从表单里取参数
std::string formOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    const auto& kv = req->form_kv();   // 获取 x-www-form-urlencoded 的键值对
    auto it = kv.find(key);
    return (it != kv.end()) ? it->second : std::string();
}

} // anonymous namespace

namespace handler {

// -------- 注册 --------
void signup(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = formOrEmpty(req, "username");
    std::string password = formOrEmpty(req, "password");
    if (username.empty() || password.empty())
        return fail(resp, "username or password missing");

    try {
        auto conn = MySQL::getConnection();

        // 1. 用户名是否已存在
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement("SELECT id FROM tbl_user WHERE username = ?")
            );
            ps->setString(1, username);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (rs->next())
                return fail(resp, "username already exists");
        }

        // 2. 生成 salt，计算哈希
        std::string salt = crypto::random_hex(16);
        std::string hash = crypto::sha256_hex(password + salt);

        // 3. 插入
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "INSERT INTO tbl_user(username, password, salt) VALUES(?, ?, ?)"
            )
        );
        ps->setString(1, username);
        ps->setString(2, hash);
        ps->setString(3, salt);
        ps->executeUpdate();

        ok(resp);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 登录 --------
void signin(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = formOrEmpty(req, "username");
    std::string password = formOrEmpty(req, "password");
    if (username.empty() || password.empty())
        return fail(resp, "username or password missing");

    try {
        auto conn = MySQL::getConnection();

        int uid = 0;
        std::string salt, stored_hash;
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "SELECT id, password, salt FROM tbl_user "
                    "WHERE username = ? AND tomb = 0"
                )
            );
            ps->setString(1, username);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (!rs->next())
                return fail(resp, "user not found");
            uid = rs->getInt("id");
            stored_hash = rs->getString("password");
            salt = rs->getString("salt");
        }

        // 校验密码
        std::string input_hash = crypto::sha256_hex(password + salt);
        if (input_hash != stored_hash)
            return fail(resp, "password incorrect");

        // 先清掉该用户已过期的 Token
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "DELETE FROM tbl_user_token WHERE uid = ? AND expired_at < NOW()"
                )
            );
            ps->setInt(1, uid);
            ps->executeUpdate();
        }
            
        // 生成 token 并写入 token 表
        std::string token = crypto::random_hex(32);
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "INSERT INTO tbl_user_token(uid, token, expired_at) "
                    "VALUES(?, ?, DATE_ADD(NOW(), INTERVAL 1 DAY))"
                )
            );
            ps->setInt(1, uid);
            ps->setString(2, token);
            ps->executeUpdate();
        }

        json data;
        data["Username"] = username;
        data["Token"]    = token;
        data["Location"] = "/static/view/home.html";
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 获取用户信息 --------
void info(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement("SELECT created_at FROM tbl_user WHERE id = ?")
        );
        ps->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (!rs->next())
            return fail(resp, "user not found");

        json data;
        data["Username"] = username;
        data["SignupAt"] = rs->getString("created_at");
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

} // namespace handler
