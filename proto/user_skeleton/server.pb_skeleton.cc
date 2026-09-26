#include "user.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

class UserServiceServiceImpl : public ::webdisk::UserService::Service
{
public:

	void Signup(::webdisk::SignupReq *request, ::webdisk::SignupResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void Signin(::webdisk::SigninReq *request, ::webdisk::SigninResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void GetUserInfo(::webdisk::GetUserInfoReq *request, ::webdisk::GetUserInfoResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}

	void CheckToken(::webdisk::CheckTokenReq *request, ::webdisk::CheckTokenResp *response, srpc::RPCContext *ctx) override
	{
		// TODO: fill server logic here
	}
};

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	unsigned short port = 1412;
	SRPCServer server;

	UserServiceServiceImpl userservice_impl;
	server.add_service(&userservice_impl);

	server.start(port);
	wait_group.wait();
	server.stop();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
