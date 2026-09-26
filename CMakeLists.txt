cmake_minimum_required(VERSION 3.10)
project(webdisk CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 标准库 ----
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Threads REQUIRED)

# ---- 手动安装的库 ----
find_library(WORKFLOW_LIB     NAMES workflow       HINTS /usr/local/lib)
find_library(WFREST_LIB       NAMES wfrest         HINTS /usr/local/lib)
find_library(MYSQLCPPCONN_LIB NAMES mysqlcppconn   HINTS /usr/lib/x86_64-linux-gnu /usr/local/lib)
find_library(SIMPLEAMQP_LIB   NAMES SimpleAmqpClient HINTS /usr/local/lib)
find_library(RABBITMQ_LIB     NAMES rabbitmq       HINTS /usr/local/lib)
find_library(SRPC_LIB         NAMES srpc          HINTS /usr/local/lib)
find_library(PROTOBUF_LIB     NAMES protobuf      HINTS /usr/lib/x86_64-linux-gnu /usr/local/lib)

find_path(WORKFLOW_INCLUDE_DIR NAMES workflow/WFTask.h    HINTS /usr/local/include)
find_path(WFREST_INCLUDE_DIR   NAMES wfrest/HttpServer.h  HINTS /usr/local/include)

if(NOT WORKFLOW_LIB OR NOT WFREST_LIB)
    message(FATAL_ERROR "workflow 或 wfrest 未找到")
endif()
if(NOT MYSQLCPPCONN_LIB)
    message(FATAL_ERROR "mysqlcppconn 未找到")
endif()
if(NOT SIMPLEAMQP_LIB OR NOT RABBITMQ_LIB)
    message(FATAL_ERROR "SimpleAmqpClient 或 rabbitmq-c 未找到")
endif()
if(NOT SRPC_LIB)
    message(FATAL_ERROR "srpc 未找到")
endif()

# ---- 共享库 common ----
add_library(common STATIC
    src/common/db/mysql.cpp
    src/common/util/crypto.cpp
    src/common/util/auth.cpp
    src/common/storage/local_storage.cpp
    src/common/storage/storage_manager.cpp
    src/common/mq/producer.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${WORKFLOW_INCLUDE_DIR}
    ${WFREST_INCLUDE_DIR}
)

target_link_libraries(common PUBLIC
    ${MYSQLCPPCONN_LIB}
    ${SIMPLEAMQP_LIB}
    ${RABBITMQ_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
    z
    pthread
)

# ---- webdisk（HTTP 服务器）----
add_executable(webdisk
    src/main.cpp
    src/handler/user_handler.cpp
    src/handler/file_handler.cpp
)

target_link_libraries(webdisk PRIVATE
    common
    ${WFREST_LIB}
    ${WORKFLOW_LIB}
)

# ---- backup_worker（MQ 消费者）----
add_executable(backup_worker src/backup_worker.cpp)

target_link_libraries(backup_worker PRIVATE common)

# ---- user_service（RPC 服务）----
add_executable(user_service
    src/user_service/main.cpp
    src/user_service/user_service_impl.cpp
    proto/user.pb.cc
)

target_include_directories(user_service PRIVATE
    ${CMAKE_SOURCE_DIR}/proto
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(user_service PRIVATE
    common
    srpc
    workflow
    ${PROTOBUF_LIB}
)

# ---- file_service（RPC 服务）----
add_executable(file_service
    src/file_service/main.cpp
    src/file_service/file_service_impl.cpp
    src/file_service/user_client.cpp
    proto/file.pb.cc
    proto/user.pb.cc
)

target_include_directories(file_service PRIVATE
    ${CMAKE_SOURCE_DIR}/proto
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(file_service PRIVATE
    common
    srpc
    workflow
    ${PROTOBUF_LIB}
)

# ---- gateway（HTTP 网关）----
add_executable(gateway
    src/gateway/main.cpp
    src/gateway/gateway_handler.cpp
    src/gateway/rpc_clients.cpp
    proto/user.pb.cc
    proto/file.pb.cc
)

target_include_directories(gateway PRIVATE
    ${CMAKE_SOURCE_DIR}/proto
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(gateway PRIVATE
    ${WFREST_LIB}
    ${WORKFLOW_LIB}
    srpc
    workflow
    ${PROTOBUF_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
    z
    pthread
)

add_custom_command(TARGET gateway POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:gateway>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/static $<TARGET_FILE_DIR:gateway>/static
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gateway>/uploads
)

# ---- 复制资源到构建目录 ----
add_custom_command(TARGET webdisk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:webdisk>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/static $<TARGET_FILE_DIR:webdisk>/static
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:webdisk>/uploads
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:webdisk>/backup
)
