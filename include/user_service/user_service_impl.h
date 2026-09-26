#pragma once
#include "user.srpc.h"

// 用户服务实现：派生自 SRPC 生成的服务基类
class UserServiceImpl : public webdisk::UserService::Service {
public:
    void Signup(webdisk::SignupReq* request,
                webdisk::SignupResp* response,
                srpc::RPCContext* ctx) override;

    void Signin(webdisk::SigninReq* request,
                webdisk::SigninResp* response,
                srpc::RPCContext* ctx) override;

    void GetUserInfo(webdisk::GetUserInfoReq* request,
                     webdisk::GetUserInfoResp* response,
                     srpc::RPCContext* ctx) override;

    void CheckToken(webdisk::CheckTokenReq* request,
                    webdisk::CheckTokenResp* response,
                    srpc::RPCContext* ctx) override;
};
