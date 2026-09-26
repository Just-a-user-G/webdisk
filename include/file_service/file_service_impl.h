#pragma once
#include "file.srpc.h"

class FileServiceImpl : public webdisk::FileService::Service {
public:
    void FileQuery(webdisk::FileQueryReq* request,
                   webdisk::FileQueryResp* response,
                   srpc::RPCContext* ctx) override;

    void FileUpload(webdisk::FileUploadReq* request,
                    webdisk::FileUploadResp* response,
                    srpc::RPCContext* ctx) override;

    void UploadInit(webdisk::UploadInitReq* request,
                    webdisk::UploadInitResp* response,
                    srpc::RPCContext* ctx) override;

    void UploadChunk(webdisk::UploadChunkReq* request,
                     webdisk::UploadChunkResp* response,
                     srpc::RPCContext* ctx) override;

    void UploadComplete(webdisk::UploadCompleteReq* request,
                        webdisk::UploadCompleteResp* response,
                        srpc::RPCContext* ctx) override;

    void UploadStatus(webdisk::UploadStatusReq* request,
                      webdisk::UploadStatusResp* response,
                      srpc::RPCContext* ctx) override;

    void FileDownload(webdisk::FileDownloadReq* request,
                      webdisk::FileDownloadResp* response,
                      srpc::RPCContext* ctx) override;

    void FileDelete(webdisk::FileDeleteReq* request,
                    webdisk::FileDeleteResp* response,
                    srpc::RPCContext* ctx) override;

    void FileRename(webdisk::FileRenameReq* request,
                    webdisk::FileRenameResp* response,
                    srpc::RPCContext* ctx) override;
};
