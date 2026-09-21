#pragma once
#include <wfrest/HttpServer.h>

namespace handler {
    // 文件列表：POST /file/query
    void fileQuery(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 文件上传：POST /file/upload
    void fileUpload(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 文件下载：GET /file/download
    void fileDownload(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 删除文件：POST /file/delete
    void fileDelete(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 重命名文件：POST /file/rename
    void fileRename(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 分片上传：初始化
    void uploadInit(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 分片上传：接收一个分片
    void uploadChunk(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 分片上传：合并
    void uploadComplete(const wfrest::HttpReq* req, wfrest::HttpResp* resp);

    // 分片上传：查询进度
    void uploadStatus(const wfrest::HttpReq* req, wfrest::HttpResp* resp);
}
