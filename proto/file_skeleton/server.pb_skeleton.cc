#include "file.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

class FileServiceServiceImpl : public ::webdisk::FileService::Service
{
public:

	void FileQuery(::webdisk::FileQueryReq *request, ::webdisk::FileQueryResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void FileUpload(::webdisk::FileUploadReq *request, ::webdisk::FileUploadResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void UploadInit(::webdisk::UploadInitReq *request, ::webdisk::UploadInitResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void UploadChunk(::webdisk::UploadChunkReq *request, ::webdisk::UploadChunkResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void UploadComplete(::webdisk::UploadCompleteReq *request, ::webdisk::UploadCompleteResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void UploadStatus(::webdisk::UploadStatusReq *request, ::webdisk::UploadStatusResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void FileDownload(::webdisk::FileDownloadReq *request, ::webdisk::FileDownloadResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void FileDelete(::webdisk::FileDeleteReq *request, ::webdisk::FileDeleteResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void FileRename(::webdisk::FileRenameReq *request, ::webdisk::FileRenameResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}
};

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	unsigned short port = 1412;
	SRPCServer server;

	FileServiceServiceImpl fileservice_impl;
	server.add_service(&fileservice_impl);

	server.start(port);
	wait_group.wait();
	server.stop();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
