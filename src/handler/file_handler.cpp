#include "handler/file_handler.h"
#include "common/storage/storage_manager.h"
#include "common/mq/producer.h"
#include "common/db/mysql.h"
#include "common/util/auth.h"
#include "common/util/crypto.h"
#include <nlohmann/json.hpp>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <fstream>
#include <cctype>
#include <vector>

using json = nlohmann::json;

namespace {
// 普通上传文件大小上限：10MB
const size_t MAX_UPLOAD_SIZE = 10 * 1024 * 1024;

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

// 根据文件名后缀推断 MIME 类型
std::string guessMimeType(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = filename.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(c);

    if (ext == "txt")  return "text/plain; charset=utf-8";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")  return "text/css";
    if (ext == "js")   return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "pdf")  return "application/pdf";
    if (ext == "zip")  return "application/zip";
    return "application/octet-stream";
}

std::string queryOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    return req->query(key);
}

std::string formOrEmpty(const wfrest::HttpReq* req, const std::string& key) {
    const auto& kv = req->form_kv();
    auto it = kv.find(key);
    return (it != kv.end()) ? it->second : std::string();
}

} // anonymous namespace

namespace handler {

// -------- 文件列表 --------
void fileQuery(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    // 解析 limit，默认 100
    int limit = 100;
    std::string limit_str = formOrEmpty(req, "limit");
    if (!limit_str.empty()) {
        try { limit = std::stoi(limit_str); }
        catch (...) { return fail(resp, "invalid limit"); }
    }
    if (limit <= 0) limit = 100;
    if (limit > 1000) limit = 1000;

    // 解析 offset，默认 0
    int offset = 0;
    std::string offset_str = formOrEmpty(req, "offset");
    if (!offset_str.empty()) {
        try { offset = std::stoi(offset_str); }
        catch (...) { return fail(resp, "invalid offset"); }
    }
    if (offset < 0) offset = 0;
    // 解析 keyword，可选
    std::string keyword = formOrEmpty(req, "keyword");
    if (keyword.size() > 255)
        return fail(resp, "keyword too long (max 255)");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

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

        json arr = json::array();
        while (rs->next()) {
            json item;
            item["FileHash"]    = rs->getString("hashcode");
            item["FileName"]    = rs->getString("filename");
            item["FileSize"]    = rs->getInt64("size");
            item["UploadAt"]    = rs->getString("created_at");
            item["LastUpdated"] = rs->getString("last_update");
            arr.push_back(item);
        }
        ok(resp, arr);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 文件上传 --------
void fileUpload(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    // 1. 从 multipart 中取文件
    wfrest::Form& form = req->form();
    auto it = form.find("file");
    if (it == form.end())
        return fail(resp, "no file field");

    std::string filename = it->second.first;    // 原始文件名
    std::string content  = it->second.second;   // 文件内容
    if (filename.empty())
        return fail(resp, "empty filename");
    if (content.size() > MAX_UPLOAD_SIZE)
        return fail(resp, "file too large (max 10MB)");
    if (content.empty())
        return fail(resp, "empty file");

    // 2. 计算 SHA256
    std::string hashcode = crypto::sha256_hex(content);
    long long   filesize = (long long)content.size();

    // 3. 保存文件
    if (!storage_manager::get()->put(hashcode, content))
        return fail(resp, "cannot save file");

    // 4. 写数据库
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
        return fail(resp, std::string("db error: ") + e.what());
    }

    // 4.5 发消息到 MQ，异步备份
    {
        json msg;
        msg["hashcode"]    = hashcode;
        msg["filename"]    = filename;
        msg["uid"]         = uid;
        msg["size"]        = filesize;
        Producer::instance().publish(msg.dump());
    }

    // 5. 返回结果
    json data;
    data["FileHash"] = hashcode;
    data["FileName"] = filename;
    data["FileSize"] = filesize;
    ok(resp, data);
}

// -------- 文件下载 --------
void fileDownload(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    std::string filename = queryOrEmpty(req, "filename");
    std::string filehash = queryOrEmpty(req, "filehash");

    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");
    if (filename.empty() || filehash.empty())
        return fail(resp, "filename or filehash missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    // 1. 确认文件属于该用户
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
        ps->setString(3, filehash);
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (!rs->next())
            return fail(resp, "file not found");
        filesize = rs->getInt64("size");
    } catch (sql::SQLException& e) {
        return fail(resp, std::string("db error: ") + e.what());
    }

    // 2. 读文件内容
    std::string content;
    if (!storage_manager::get()->get(filehash, content))
        return fail(resp, "cannot read file");

    // 3. 设置响应头
    resp->headers["Content-Disposition"] =
        "attachment; filename=\"" + filename + "\"";
    resp->headers["Content-Type"] = guessMimeType(filename);

    // 4. 返回内容
    resp->String(content);
}

// -------- 删除文件（软删除） --------
void fileDelete(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    std::string filename = formOrEmpty(req, "filename");
    std::string filehash = formOrEmpty(req, "filehash");
    if (filename.empty() || filehash.empty())
        return fail(resp, "filename or filehash missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "UPDATE tbl_file SET status = 1 "
                "WHERE uid = ? AND filename = ? AND hashcode = ? AND status = 0"
            )
        );
        ps->setInt(1, uid);
        ps->setString(2, filename);
        ps->setString(3, filehash);
        int affected = ps->executeUpdate();

        if (affected == 0)
            return fail(resp, "file not found");

        ok(resp);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 重命名文件 --------
void fileRename(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    std::string old_name = formOrEmpty(req, "old_filename");
    std::string new_name = formOrEmpty(req, "new_filename");
    std::string filehash = formOrEmpty(req, "filehash");

    if (old_name.empty() || new_name.empty() || filehash.empty())
        return fail(resp, "old_filename / new_filename / filehash missing");
    if (new_name.size() > 255)
        return fail(resp, "new filename too long (max 255)");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    try {
        auto conn = MySQL::getConnection();
        std::unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "UPDATE tbl_file SET filename = ? "
                "WHERE uid = ? AND filename = ? AND hashcode = ? AND status = 0"
            )
        );
        ps->setString(1, new_name);
        ps->setInt(2, uid);
        ps->setString(3, old_name);
        ps->setString(4, filehash);
        int affected = ps->executeUpdate();

        if (affected == 0)
            return fail(resp, "file not found");

        ok(resp);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：初始化 --------
void uploadInit(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    // 解析参数
    std::string filename  = formOrEmpty(req, "filename");
    std::string chunks_s  = formOrEmpty(req, "total_chunks");
    std::string size_s    = formOrEmpty(req, "total_size");

    if (filename.empty() || chunks_s.empty() || size_s.empty())
        return fail(resp, "filename / total_chunks / total_size missing");
    if (filename.size() > 255)
        return fail(resp, "filename too long (max 255)");

    int total_chunks = 0;
    long long total_size = 0;
    try {
        total_chunks = std::stoi(chunks_s);
        total_size   = std::stoll(size_s);
    } catch (...) {
        return fail(resp, "invalid total_chunks or total_size");
    }

    if (total_chunks <= 0 || total_chunks > 10000)
        return fail(resp, "total_chunks out of range (1-10000)");
    if (total_size <= 0 || total_size > (long long)5 * 1024 * 1024 * 1024)
        return fail(resp, "total_size out of range (1B-5GB)");

    // 清理该用户超过 24 小时未完成的会话
    try {
        auto conn = MySQL::getConnection();

        // 1. 先查出要清理的会话 ID
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

        // 2. 删临时目录
        for (const auto& sid : stale_ids) {
            std::string cmd = "rm -rf uploads/tmp/" + sid;
            std::system(cmd.c_str());
        }

        // 3. 删数据库记录
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
    } catch (sql::SQLException& e) {
        // 清理失败不影响正常上传，只记录
        std::cerr << "cleanup stale sessions failed: " << e.what() << "\n";
    }

    // 生成 upload_id
    std::string upload_id = crypto::random_hex(32);

    // 创建临时目录 uploads/tmp/<upload_id>/
    std::string tmp_dir = "uploads/tmp/" + upload_id;
    std::string mkdir_cmd = "mkdir -p " + tmp_dir;
    if (std::system(mkdir_cmd.c_str()) != 0)
        return fail(resp, "cannot create temp dir");

    // 写入数据库
    try {
        auto conn = MySQL::getConnection();
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

        json data;
        data["UploadID"]     = upload_id;
        data["TotalChunks"]  = total_chunks;
        data["TotalSize"]    = total_size;
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：接收一个分片 --------
void uploadChunk(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    std::string upload_id   = queryOrEmpty(req, "upload_id");
    std::string chunk_index_s = queryOrEmpty(req, "chunk_index");
    if (upload_id.empty() || chunk_index_s.empty())
        return fail(resp, "upload_id or chunk_index missing");

    int chunk_index = 0;
    try { chunk_index = std::stoi(chunk_index_s); }
    catch (...) { return fail(resp, "invalid chunk_index"); }
    if (chunk_index < 0)
        return fail(resp, "chunk_index must be >= 0");

    // 取分片数据
    wfrest::Form& form = req->form();
    auto it = form.find("chunk");
    if (it == form.end())
        return fail(resp, "no chunk field");
    std::string chunk_data = it->second.second;
    if (chunk_data.empty())
        return fail(resp, "empty chunk");

    try {
        auto conn = MySQL::getConnection();

        // 1. 校验会话存在、属于当前用户、状态进行中
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
            if (!rs->next())
                return fail(resp, "upload session not found or not active");
            total_chunks = rs->getInt("total_chunks");
        }

        if (chunk_index >= total_chunks)
            return fail(resp, "chunk_index out of range");

        // 2. 写分片文件
        std::string chunk_path =
            "uploads/tmp/" + upload_id + "/" + std::to_string(chunk_index);
        {
            std::ofstream ofs(chunk_path, std::ios::binary);
            if (!ofs.is_open())
                return fail(resp, "cannot save chunk");
            ofs.write(chunk_data.data(), chunk_data.size());
        }

        // 3. 重新扫描临时目录，统计已收到的分片数（幂等）
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

        json data;
        data["UploadID"]   = upload_id;
        data["ChunkIndex"] = chunk_index;
        data["ChunkSize"]  = (long long)chunk_data.size();
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：合并 --------
void uploadComplete(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    std::string upload_id = queryOrEmpty(req, "upload_id");
    if (upload_id.empty())
        return fail(resp, "upload_id missing");

    try {
        auto conn = MySQL::getConnection();

        // 1. 查会话
        std::string filename;
        int total_chunks = 0;
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
            if (!rs->next())
                return fail(resp, "upload session not found or not active");
            filename        = rs->getString("filename");
            total_chunks    = rs->getInt("total_chunks");
            received_chunks = rs->getInt("received_chunks");
        }

        if (received_chunks < total_chunks)
            return fail(resp, "not all chunks received");

        // 2. 流式合并：边读分片边写临时文件，同时算 hash
        std::string tmp_dir = "uploads/tmp/" + upload_id;
        std::string tmp_merge = tmp_dir + "/merged";

        std::ofstream out(tmp_merge, std::ios::binary);
        if (!out.is_open())
            return fail(resp, "cannot create merge file");

        crypto::Sha256Hasher hasher;
        long long filesize = 0;
        const size_t BUF_SIZE = 64 * 1024;
        std::string buf(BUF_SIZE, '\0');

        for (int i = 0; i < total_chunks; ++i) {
            std::string part = tmp_dir + "/" + std::to_string(i);
            std::ifstream ifs(part, std::ios::binary);
            if (!ifs.is_open())
                return fail(resp, "missing chunk " + std::to_string(i));

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
        if (!out.good())
            return fail(resp, "write merged file failed");

        std::string hashcode = hasher.final_hex();

        // 3. 把合并后的文件移入 Storage
        if (!storage_manager::get()->putFromFile(hashcode, tmp_merge))
            return fail(resp, "cannot save merged file");

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

        // 4.5 发消息到 MQ，异步备份
        {
            json msg;
            msg["hashcode"]    = hashcode;
            msg["filename"]    = filename;
            msg["uid"]         = uid;
            msg["size"]        = filesize;
            Producer::instance().publish(msg.dump());
        }

        // 5. 标记会话完成
        {
            std::unique_ptr<sql::PreparedStatement> ps(
                conn->prepareStatement(
                    "UPDATE tbl_upload_session SET status = 1 WHERE id = ?"
                )
            );
            ps->setString(1, upload_id);
            ps->executeUpdate();
        }

        // 6. 删临时目录
        std::string rm_cmd = "rm -rf " + tmp_dir;
        std::system(rm_cmd.c_str());

        json data;
        data["FileHash"] = hashcode;
        data["FileName"] = filename;
        data["FileSize"] = filesize;
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}

// -------- 分片上传：查询进度 --------
void uploadStatus(const wfrest::HttpReq* req, wfrest::HttpResp* resp) {
    std::string username = queryOrEmpty(req, "username");
    std::string token    = queryOrEmpty(req, "token");
    if (username.empty() || token.empty())
        return fail(resp, "username or token missing");

    int uid = 0;
    if (!auth::checkToken(username, token, uid))
        return fail(resp, "invalid token");

    std::string upload_id = queryOrEmpty(req, "upload_id");
    if (upload_id.empty())
        return fail(resp, "upload_id missing");

    try {
        auto conn = MySQL::getConnection();

        // 1. 查会话信息
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
            if (!rs->next())
                return fail(resp, "upload session not found or not active");
            filename     = rs->getString("filename");
            total_chunks = rs->getInt("total_chunks");
        }

        // 2. 扫描临时目录，列出已收到的分片索引
        json received = json::array();
        std::string tmp_dir = "uploads/tmp/" + upload_id;
        for (int i = 0; i < total_chunks; ++i) {
            std::string part = tmp_dir + "/" + std::to_string(i);
            std::ifstream ifs(part, std::ios::binary);
            if (ifs.is_open()) received.push_back(i);
        }

        json data;
        data["UploadID"]        = upload_id;
        data["FileName"]        = filename;
        data["TotalChunks"]     = total_chunks;
        data["ReceivedChunks"]  = received;
        ok(resp, data);
    } catch (sql::SQLException& e) {
        fail(resp, std::string("db error: ") + e.what());
    }
}
} // namespace handler
