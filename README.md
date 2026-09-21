# Web 网盘

基于 C++17 的 Web 网盘项目，第一期实现用户注册、登录、文件上传、下载、列表。

## 技术栈

- C++17
- CMake
- wfrest + workflow（HTTP 服务）
- MySQL 8 + MySQL Connector/C++
- OpenSSL（SHA256、随机数）
- nlohmann/json

## 依赖安装

### 系统依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake zlib1g-dev libssl-dev \
    nlohmann-json3-dev libmysqlcppconn-dev
workflow
bash
git clone https://github.com/sogou/workflow.git
cd workflow && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
sudo ldconfig
wfrest
bash
git clone --recursive https://github.com/wfrest/wfrest.git
cd wfrest && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
sudo ldconfig
数据库初始化
bash
mysql -u root -p < sql/init.sql
配置
编辑 config/config.json：

json
{
  "mysql": {
    "host": "tcp://127.0.0.1:3306",
    "user": "root",
    "password": "你的密码",
    "database": "webdisk"
  },
  "server": {
    "port": 8888
  },
  "storage": {
    "upload_dir": "uploads"
  },
  "token": {
    "expire_seconds": 86400
  }
}
编译
bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
运行
bash
cd build
./webdisk
服务默认监听 http://127.0.0.1:8888。

接口
方法	路径	说明
GET	/ping	健康检查
POST	/user/signup	注册
POST	/user/signin	登录
GET	/user/info	获取用户信息
POST	/file/query	文件列表
POST	/file/upload	上传文件
GET	/file/download	下载文件
所有接口统一返回：

json
{"code": 0, "msg": "SUCCESS", "data": {...}}
code 为 0 表示成功，非 0 表示失败。

测试
注册
bash
curl -s -X POST http://127.0.0.1:8888/user/signup \
  -d 'username=alice&password=123456'
登录
bash
curl -s -X POST http://127.0.0.1:8888/user/signin \
  -d 'username=alice&password=123456'
返回里的 Token 后面要用。

用户信息
bash
curl -s -G http://127.0.0.1:8888/user/info \
  --data-urlencode 'username=alice' \
  --data-urlencode 'token=<token>'
文件列表
bash
curl -s -X POST 'http://127.0.0.1:8888/file/query?username=alice&token=<token>' \
  -d 'limit=10'
上传
bash
curl -s -X POST 'http://127.0.0.1:8888/file/upload?username=alice&token=<token>' \
  -F 'file=@/tmp/a.txt'
下载
bash
curl -s -G 'http://127.0.0.1:8888/file/download' \
  --data-urlencode 'username=alice' \
  --data-urlencode 'token=<token>' \
  --data-urlencode 'filename=a.txt' \
  --data-urlencode 'filehash=<filehash>' \
  -o /tmp/downloaded.txt
目录结构
text
webdisk/
├── CMakeLists.txt
├── config/config.json
├── sql/init.sql
├── include/
│   ├── db/mysql.h
│   ├── handler/user_handler.h
│   ├── handler/file_handler.h
│   └── util/{crypto.h, auth.h}
├── src/
│   ├── main.cpp
│   ├── db/mysql.cpp
│   ├── handler/{user_handler.cpp, file_handler.cpp}
│   └── util/{crypto.cpp, auth.cpp}
├── tests/
├── uploads/
└── README.md
说明
文件物理存储名为内容 SHA256，天然去重，避免文件名冲突

密码存储为 sha256(password + salt)，每个用户独立 salt

Token 存 tbl_user_token，24 小时过期，登录时清理该用户过期 Token

上传大小上限 10MB

下载设置 Content-Disposition 和 Content-Type