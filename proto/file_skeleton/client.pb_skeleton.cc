#include "file.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

static void filequery_done(::webdisk::FileQueryResp *response, srpc::RPCContext *context)
{
}

static void fileupload_done(::webdisk::FileUploadResp *response, srpc::RPCContext *context)
{
}

static void uploadinit_done(::webdisk::UploadInitResp *response, srpc::RPCContext *context)
{
}

static void uploadchunk_done(::webdisk::UploadChunkResp *response, srpc::RPCContext *context)
{
}

static void uploadcomplete_done(::webdisk::UploadCompleteResp *response, srpc::RPCContext *context)
{
}

static void uploadstatus_done(::webdisk::UploadStatusResp *response, srpc::RPCContext *context)
{
}

static void filedownload_done(::webdisk::FileDownloadResp *response, srpc::RPCContext *context)
{
}

static void filedelete_done(::webdisk::FileDeleteResp *response, srpc::RPCContext *context)
{
}

static void filerename_done(::webdisk::FileRenameResp *response, srpc::RPCContext *context)
{
}

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	const char *ip = "127.0.0.1";
	unsigned short port = 1412;

	::webdisk::FileService::SRPCClient client(ip, port);

	// example for RPC method call
	::webdisk::FileQueryReq filequery_req;
	//filequery_req.set_message("Hello, srpc!");
	client.FileQuery(&filequery_req, filequery_done);

	wait_group.wait();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
