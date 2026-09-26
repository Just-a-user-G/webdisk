#include "file_service/file_service_impl.h"
#include "file_service/user_client.h"
#include "common/db/mysql.h"
#include "common/util/crypto.h"
#include "common/storage/storage_manager.h"
#include "common/mq/producer.h"
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using namespace webdisk;

// 普通上传大小上限：10MB
static const size_t MAX_UPLOAD_SIZE = 10 * 1024 * 1024;

// 小工具：统一的错误响应
static void fail(google::protobuf::Message* resp, int code, const std::string& msg) {
    // 用反射设置 code / msg（所有 Resp 都有这两个字段）
    const google::protobuf::Descriptor* desc = resp->GetDescriptor();
    const google::protobuf::Reflection* refl = resp->GetReflection();
    const google::protobuf::FieldDescriptor* code_field = desc->FindFieldByName("code");
    const google::protobuf::FieldDescriptor* msg_field  = desc->FindFieldByName("msg");
    if (code_field) refl->SetInt32(resp, code_field, code);
    if (msg_field)  refl->SetString(resp, msg_field, msg);
}

// -------- 文件列表 --------
void FileServiceImpl::FileQuery(FileQueryReq* req, FileQueryResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    int limit  = req->limit()  > 0 ? req->limit()  : 100;
    if (limit > 1000) limit = 1000;
    int offset = req->offset() > 0 ? req->offset() : 0;
    std::string keyword = req->keyword();
    if (keyword.size() > 255) {
        resp->set_code(1);
        resp->set_msg("keyword too long");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        std::string sql =
            "SELECT filename, hashcode, size, created_at, last_update "
            "FROM tbl_file "
            "WHERE uid = ? AND status = 0 ";
        if (!keyword.empty())
            sql += "AND filename LIKE ? ";
        sql += "ORDER BY created_at DESC LIMIT ? OFFSET ?";

        std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql));
        int idx = 1;
        ps->setInt(idx++, uid);
        if (!keyword.empty())
            ps->setString(idx++, "%" + keyword + "%");
        ps->setInt(idx++, limit);
        ps->setInt(idx++, offset);

        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());

        while (rs->next()) {
            auto* item = resp->add_items();
            item->set_file_hash(rs->getString("hashcode"));
            item->set_file_name(rs->getString("filename"));
            item->set_file_size(rs->getInt64("size"));
            item->set_upload_at(rs->getString("created_at"));
            item->set_last_updated(rs->getString("last_update"));
        }

        resp->set_code(0);
        resp->set_msg("SUCCESS");
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 删除 --------
void FileServiceImpl::FileDelete(FileDeleteReq* req, FileDeleteResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    if (req->filename().empty() || req->file_hash().empty()) {
        resp->set_code(1);
        resp->set_msg("filename or file_hash missing");
        return;
    }

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "UPDATE tbl_file SET status = 1 "
                "WHERE uid = ? AND filename = ? AND hashcode = ? AND status = 0"
            )
        );
        ps->setInt(1, uid);
        ps->setString(2, req->filename());
        ps->setString(3, req->file_hash());
        int affected = ps->executeUpdate();

        if (affected == 0) {
            resp->set_code(1);
            resp->set_msg("file not found");
            return;
        }
        resp->set_code(0);
        resp->set_msg("SUCCESS");
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 重命名 --------
void FileServiceImpl::FileRename(FileRenameReq* req, FileRenameResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    if (req->old_filename().empty() || req->new_filename().empty() || req->file_hash().empty()) {
        resp->set_code(1);
        resp->set_msg("old_filename / new_filename / file_hash missing");
        return;
    }
    if (req->new_filename().size() > 255) {
        resp->set_code(1);
        resp->set_msg("new filename too long");
        return;
    }

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "UPDATE tbl_file SET filename = ? "
                "WHERE uid = ? AND filename = ? AND hashcode = ? AND status = 0"
            )
        );
        ps->setString(1, req->new_filename());
        ps->setInt(2, uid);
        ps->setString(3, req->old_filename());
        ps->setString(4, req->file_hash());
        int affected = ps->executeUpdate();

        if (affected == 0) {
            resp->set_code(1);
            resp->set_msg("file not found");
            return;
        }
        resp->set_code(0);
        resp->set_msg("SUCCESS");
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 普通上传 --------
void FileServiceImpl::FileUpload(FileUploadReq* req, FileUploadResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& filename = req->filename();
    const std::string& content  = req->content();

    if (filename.empty()) {
        resp->set_code(1);
        resp->set_msg("empty filename");
        return;
    }
    if (content.size() > MAX_UPLOAD_SIZE) {
        resp->set_code(1);
        resp->set_msg("file too large (max 10MB)");
        return;
    }
    if (content.empty()) {
        resp->set_code(1);
        resp->set_msg("empty file");
        return;
    }

    std::string hashcode = crypto::sha256_hex(content);
    long long   filesize = (long long)content.size();

    if (!storage_manager::get()->put(hashcode, content)) {
        resp->set_code(1);
        resp->set_msg("cannot save file");
        return;
    }

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "INSERT INTO tbl_file(uid, filename, hashcode, size, status) "
                "VALUES(?, ?, ?, ?, 0)"
            )
        );
        ps->setInt(1, uid);
        ps->setString(2, filename);
        ps->setString(3, hashcode);
        ps->setInt64(4, filesize);
        ps->executeUpdate();
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
        return;
    }

    // 发消息到 MQ，异步备份
    {
        nlohmann::json msg;
        msg["hashcode"] = hashcode;
        msg["filename"] = filename;
        msg["uid"]      = uid;
        msg["size"]     = filesize;
        Producer::instance().publish(msg.dump());
    }

    resp->set_code(0);
    resp->set_msg("SUCCESS");
    resp->set_file_hash(hashcode);
    resp->set_file_name(filename);
    resp->set_file_size(filesize);
}

// -------- 分片上传：初始化 --------
void FileServiceImpl::UploadInit(UploadInitReq* req, UploadInitResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& filename = req->filename();
    int       total_chunks = req->total_chunks();
    long long total_size   = req->total_size();

    if (filename.empty()) {
        resp->set_code(1);
        resp->set_msg("filename missing");
        return;
    }
    if (filename.size() > 255) {
        resp->set_code(1);
        resp->set_msg("filename too long (max 255)");
        return;
    }
    if (total_chunks <= 0 || total_chunks > 10000) {
        resp->set_code(1);
        resp->set_msg("total_chunks out of range (1-10000)");
        return;
    }
    if (total_size <= 0 || total_size > (long long)5 * 1024 * 1024 * 1024) {
        resp->set_code(1);
        resp->set_msg("total_size out of range (1B-5GB)");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        // 清理该用户超过 24 小时未完成的会话
        {
            std::vector<std::string> stale_ids;
            {
                std::unique_ptr<sql::PreparedStatement> ps(
                    conn->prepareStatement(
                        "SELECT id FROM tbl_upload_session "
                        "WHERE uid = ? AND status = 0 "
                        "AND created_at < NOW() - INTERVAL 24 HOUR"
                    )
                );
                ps->setInt(1, uid);
                std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
                while (rs->next())
                    stale_ids.push_back(rs->getString("id"));
            }
            for (const auto& sid : stale_ids) {
                std::string cmd = "rm -rf uploads/tmp/" + sid;
                std::system(cmd.c_str());
            }
            if (!stale_ids.empty()) {
                std::unique_ptr<sql::PreparedStatement> ps(
                    conn->prepareStatement(
                        "DELETE FROM tbl_upload_session "
                        "WHERE uid = ? AND status = 0 "
                        "AND created_at < NOW() - INTERVAL 24 HOUR"
                    )
                );
                ps->setInt(1, uid);
                ps->executeUpdate();
            }
        }

        // 生成 upload_id
        std::string upload_id = crypto::random_hex(32);

        // 创建临时目录
        std::string tmp_dir = "uploads/tmp/" + upload_id;
        std::string mkdir_cmd = "mkdir -p " + tmp_dir;
        if (std::system(mkdir_cmd.c_str()) != 0) {
            resp->set_code(1);
            resp->set_msg("cannot create temp dir");
            return;
        }

        // 写入会话
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "INSERT INTO tbl_upload_session"
                "(id, uid, filename, total_chunks, received_chunks, total_size, status) "
                "VALUES(?, ?, ?, ?, 0, ?, 0)"
            )
        );
        ps->setString(1, upload_id);
        ps->setInt(2, uid);
        ps->setString(3, filename);
        ps->setInt(4, total_chunks);
        ps->setInt64(5, total_size);
        ps->executeUpdate();

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_upload_id(upload_id);
        resp->set_total_chunks(total_chunks);
        resp->set_total_size(total_size);
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：接收分片 --------
void FileServiceImpl::UploadChunk(UploadChunkReq* req, UploadChunkResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& upload_id   = req->upload_id();
    int                chunk_index = req->chunk_index();
    const std::string& chunk_data  = req->chunk_data();

    if (upload_id.empty()) {
        resp->set_code(1);
        resp->set_msg("upload_id missing");
        return;
    }
    if (chunk_index < 0) {
        resp->set_code(1);
        resp->set_msg("chunk_index must be >= 0");
        return;
    }
    if (chunk_data.empty()) {
        resp->set_code(1);
        resp->set_msg("empty chunk");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        // 校验会话
        int total_chunks = 0;
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "SELECT total_chunks FROM tbl_upload_session "
                    "WHERE id = ? AND uid = ? AND status = 0"
                )
            );
            ps->setString(1, upload_id);
            ps->setInt(2, uid);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (!rs->next()) {
                resp->set_code(1);
                resp->set_msg("upload session not found or not active");
                return;
            }
            total_chunks = rs->getInt("total_chunks");
        }

        if (chunk_index >= total_chunks) {
            resp->set_code(1);
            resp->set_msg("chunk_index out of range");
            return;
        }

        // 写分片文件
        std::string chunk_path =
            "uploads/tmp/" + upload_id + "/" + std::to_string(chunk_index);
        {
            std::ofstream ofs(chunk_path, std::ios::binary);
            if (!ofs.is_open()) {
                resp->set_code(1);
                resp->set_msg("cannot save chunk");
                return;
            }
            ofs.write(chunk_data.data(), chunk_data.size());
        }

        // 重新扫描临时目录，统计已收到的分片数（幂等）
        int actual_received = 0;
        {
            std::string tmp_dir = "uploads/tmp/" + upload_id;
            for (int i = 0; i < total_chunks; ++i) {
                std::string part = tmp_dir + "/" + std::to_string(i);
                std::ifstream ifs(part, std::ios::binary);
                if (ifs.is_open()) actual_received++;
            }
        }
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "UPDATE tbl_upload_session SET received_chunks = ? WHERE id = ?"
                )
            );
            ps->setInt(1, actual_received);
            ps->setString(2, upload_id);
            ps->executeUpdate();
        }

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_upload_id(upload_id);
        resp->set_chunk_index(chunk_index);
        resp->set_chunk_size((long long)chunk_data.size());
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：合并 --------
void FileServiceImpl::UploadComplete(UploadCompleteReq* req, UploadCompleteResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& upload_id = req->upload_id();
    if (upload_id.empty()) {
        resp->set_code(1);
        resp->set_msg("upload_id missing");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        // 1. 查会话
        std::string filename;
        int total_chunks    = 0;
        int received_chunks = 0;
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "SELECT filename, total_chunks, received_chunks "
                    "FROM tbl_upload_session "
                    "WHERE id = ? AND uid = ? AND status = 0"
                )
            );
            ps->setString(1, upload_id);
            ps->setInt(2, uid);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (!rs->next()) {
                resp->set_code(1);
                resp->set_msg("upload session not found or not active");
                return;
            }
            filename        = rs->getString("filename");
            total_chunks    = rs->getInt("total_chunks");
            received_chunks = rs->getInt("received_chunks");
        }

        if (received_chunks < total_chunks) {
            resp->set_code(1);
            resp->set_msg("not all chunks received");
            return;
        }

        // 2. 流式合并
        std::string tmp_dir   = "uploads/tmp/" + upload_id;
        std::string tmp_merge = tmp_dir + "/merged";

        std::ofstream out(tmp_merge, std::ios::binary);
        if (!out.is_open()) {
            resp->set_code(1);
            resp->set_msg("cannot create merge file");
            return;
        }

        crypto::Sha256Hasher hasher;
        long long filesize = 0;
        const size_t BUF_SIZE = 64 * 1024;
        std::string buf(BUF_SIZE, '\0');

        for (int i = 0; i < total_chunks; ++i) {
            std::string part = tmp_dir + "/" + std::to_string(i);
            std::ifstream ifs(part, std::ios::binary);
            if (!ifs.is_open()) {
                resp->set_code(1);
                resp->set_msg("missing chunk " + std::to_string(i));
                return;
            }
            while (ifs) {
                ifs.read(&buf[0], BUF_SIZE);
                std::streamsize n = ifs.gcount();
                if (n > 0) {
                    hasher.update(buf.data(), (size_t)n);
                    out.write(buf.data(), n);
                    filesize += n;
                }
            }
        }
        out.close();
        if (!out.good()) {
            resp->set_code(1);
            resp->set_msg("write merged file failed");
            return;
        }

        std::string hashcode = hasher.final_hex();

        // 3. 移入 Storage
        if (!storage_manager::get()->putFromFile(hashcode, tmp_merge)) {
            resp->set_code(1);
            resp->set_msg("cannot save merged file");
            return;
        }

        // 4. 写 tbl_file
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "INSERT INTO tbl_file(uid, filename, hashcode, size, status) "
                    "VALUES(?, ?, ?, ?, 0)"
                )
            );
            ps->setInt(1, uid);
            ps->setString(2, filename);
            ps->setString(3, hashcode);
            ps->setInt64(4, filesize);
            ps->executeUpdate();
        }

        // 5. 发消息到 MQ
        {
            nlohmann::json msg;
            msg["hashcode"] = hashcode;
            msg["filename"] = filename;
            msg["uid"]      = uid;
            msg["size"]     = filesize;
            Producer::instance().publish(msg.dump());
        }

        // 6. 标记会话完成
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "UPDATE tbl_upload_session SET status = 1 WHERE id = ?"
                )
            );
            ps->setString(1, upload_id);
            ps->executeUpdate();
        }

        // 7. 删临时目录
        std::string rm_cmd = "rm -rf " + tmp_dir;
        std::system(rm_cmd.c_str());

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_file_hash(hashcode);
        resp->set_file_name(filename);
        resp->set_file_size(filesize);
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：查询进度 --------
void FileServiceImpl::UploadStatus(UploadStatusReq* req, UploadStatusResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& upload_id = req->upload_id();
    if (upload_id.empty()) {
        resp->set_code(1);
        resp->set_msg("upload_id missing");
        return;
    }

    try {
        auto conn = MySQL::getConnection();

        int total_chunks = 0;
        std::string filename;
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "SELECT filename, total_chunks FROM tbl_upload_session "
                    "WHERE id = ? AND uid = ? AND status = 0"
                )
            );
            ps->setString(1, upload_id);
            ps->setInt(2, uid);
            std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
            if (!rs->next()) {
                resp->set_code(1);
                resp->set_msg("upload session not found or not active");
                return;
            }
            filename     = rs->getString("filename");
            total_chunks = rs->getInt("total_chunks");
        }

        std::string tmp_dir = "uploads/tmp/" + upload_id;
        for (int i = 0; i < total_chunks; ++i) {
            std::string part = tmp_dir + "/" + std::to_string(i);
            std::ifstream ifs(part, std::ios::binary);
            if (ifs.is_open()) resp->add_received_chunks(i);
        }

        resp->set_code(0);
        resp->set_msg("SUCCESS");
        resp->set_upload_id(upload_id);
        resp->set_file_name(filename);
        resp->set_total_chunks(total_chunks);
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }
}

// -------- 下载 --------
void FileServiceImpl::FileDownload(FileDownloadReq* req, FileDownloadResp* resp, srpc::RPCContext* ctx) {
    int uid = 0;
    if (!UserClient::instance().checkToken(req->username(), req->token(), uid)) {
        resp->set_code(1);
        resp->set_msg("invalid token");
        return;
    }

    const std::string& filename  = req->filename();
    const std::string& file_hash = req->file_hash();

    if (filename.empty() || file_hash.empty()) {
        resp->set_code(1);
        resp->set_msg("filename or file_hash missing");
        return;
    }

    // 确认文件属于该用户
    long long filesize = 0;
    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "SELECT size FROM tbl_file "
                "WHERE uid = ? AND filename = ? AND hashcode = ? AND status = 0"
            )
        );
        ps->setInt(1, uid);
        ps->setString(2, filename);
        ps->setString(3, file_hash);
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (!rs->next()) {
            resp->set_code(1);
            resp->set_msg("file not found");
            return;
        }
        filesize = rs->getInt64("size");
    } catch (sql::SQLException& e) {
        resp->set_code(1);
        resp->set_msg(std::string("db error: ") + e.what());
    }

    std::string content;
    if (!storage_manager::get()->get(file_hash, content)) {
        resp->set_code(1);
        resp->set_msg("cannot read file");
        return;
    }

    resp->set_code(0);
    resp->set_msg("SUCCESS");
    resp->set_content(content);
    resp->set_filename(filename);
    resp->set_file_size(filesize);
}
