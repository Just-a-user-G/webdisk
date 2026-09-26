#include "user_service/user_service_impl.h"
#include "common/db/mysql.h"
#include "common/util/crypto.h"
#include "common/util/auth.h"
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <iostream>

using namespace webdisk;

// -------- 注册 --------
void UserServiceImpl::Signup(SignupReq* req, SignupResp* resp, srpc::RPCContext* ctx) {
    const std::string& username = req->username();
    const std::string& password = req->password();

    if (username.empty() || password.empty()) {
        resp->set_code(1);
        resp->set_msg("username or password missing");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        // 1. 检查重名
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement("SELECT id FROM tbl_user WHERE username = ?")
            );
            ps->setString(1, username);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (rs->next()) {
                resp->set_code(1);
                resp->set_msg("username already exists");
                return;
            }
        }

        // 2. 生成 salt + 哈希
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

        resp->set_code(0);
        resp->set_msg("SUCCESS");
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 登录 --------
void UserServiceImpl::Signin(SigninReq* req, SigninResp* resp, srpc::RPCContext* ctx) {
    const std::string& username = req->username();
    const std::string& password = req->password();

    if (username.empty() || password.empty()) {
        resp->set_code(1);
        resp->set_msg("username or password missing");
        return;
    }

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
            if (!rs->next()) {
                resp->set_code(1);
                resp->set_msg("user not found");
                return;
            }
            uid = rs->getInt("id");
            stored_hash = rs->getString("password");
            salt = rs->getString("salt");
        }

        // 校验密码
        std::string input_hash = crypto::sha256_hex(password + salt);
        if (input_hash != stored_hash) {
            resp->set_code(1);
            resp->set_msg("password incorrect");
            return;
        }

        // 清理过期 Token
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "DELETE FROM tbl_user_token WHERE uid = ? AND expired_at < NOW()"
                )
            );
            ps->setInt(1, uid);
            ps->executeUpdate();
        }

        // 生成新 Token
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

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_token(token);
        resp->set_username(username);
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 获取用户信息 --------
void UserServiceImpl::GetUserInfo(GetUserInfoReq* req, GetUserInfoResp* resp, srpc::RPCContext* ctx) {
    const std::string& username = req->username();
    const std::string& token    = req->token();

    if (username.empty() || token.empty()) {
        resp->set_code(1);
        resp->set_msg("username or token missing");
        return;
    }

    int uid = 0;
    if (!auth::checkToken(username, token, uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement("SELECT created_at FROM tbl_user WHERE id = ?")
        );
        ps->setInt(1, uid);
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (!rs->next()) {
            resp->set_code(1);
            resp->set_msg("user not found");
            return;
        }

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_username(username);
        resp->set_signup_at(rs->getString("created_at"));
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 校验 Token --------
void UserServiceImpl::CheckToken(CheckTokenReq* req, CheckTokenResp* resp, srpc::RPCContext* ctx) {
    const std::string& username = req->username();
    const std::string& token    = req->token();

    if (username.empty() || token.empty()) {
        resp->set_code(1);
        resp->set_msg("username or token missing");
        return;
    }

    int uid = 0;
    if (!auth::checkToken(username, token, uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    resp->set_code(0);
    resp->set_msg("SUCCESS");
    resp->set_uid(uid);
}
