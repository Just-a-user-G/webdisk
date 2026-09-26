#include "user.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

static void signup_done(::webdisk::SignupResp *response, srpc::RPCContext *context)
{
}

static void signin_done(::webdisk::SigninResp *response, srpc::RPCContext *context)
{
}

static void getuserinfo_done(::webdisk::GetUserInfoResp *response, srpc::RPCContext *context)
{
}

static void checktoken_done(::webdisk::CheckTokenResp *response, srpc::RPCContext *context)
{
}

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	const char *ip = "127.0.0.1";
	unsigned short port = 1412;

	::webdisk::UserService::SRPCClient client(ip, port);

	// example for RPC method call
	::webdisk::SignupReq signup_req;
	//signup_req.set_message("Hello, srpc!");
	client.Signup(&signup_req, signup_done);

	wait_group.wait();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
