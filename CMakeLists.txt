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
find_library(WORKFLOW_LIB     NAMES workflow     HINTS /usr/local/lib)
find_library(WFREST_LIB       NAMES wfrest       HINTS /usr/local/lib)
find_library(MYSQLCPPCONN_LIB NAMES mysqlcppconn HINTS /usr/lib/x86_64-linux-gnu /usr/local/lib)

find_path(WORKFLOW_INCLUDE_DIR NAMES workflow/WFTask.h      HINTS /usr/local/include)
find_path(WFREST_INCLUDE_DIR   NAMES wfrest/HttpServer.h    HINTS /usr/local/include)
find_library(SIMPLEAMQP_LIB   NAMES SimpleAmqpClient HINTS /usr/local/lib)
find_library(RABBITMQ_LIB     NAMES rabbitmq         HINTS /usr/local/lib)

if(NOT WORKFLOW_LIB OR NOT WFREST_LIB)
    message(FATAL_ERROR "workflow 或 wfrest 未找到")
endif()
if(NOT MYSQLCPPCONN_LIB)
    message(FATAL_ERROR "mysqlcppconn 未找到，请安装 libmysqlcppconn-dev")
endif()
if(NOT SIMPLEAMQP_LIB OR NOT RABBITMQ_LIB)
    message(FATAL_ERROR "SimpleAmqpClient 或 rabbitmq-c 未找到")
endif()

# ---- 源文件 ----
set(SOURCES
    src/main.cpp
    src/db/mysql.cpp
    src/util/crypto.cpp
    src/util/auth.cpp
    src/handler/user_handler.cpp
    src/handler/file_handler.cpp
    src/storage/local_storage.cpp
    src/storage/storage_manager.cpp
    src/mq/producer.cpp
)

add_executable(webdisk ${SOURCES})

target_include_directories(webdisk PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${WORKFLOW_INCLUDE_DIR}
    ${WFREST_INCLUDE_DIR}
)

target_link_libraries(webdisk PRIVATE
    ${WFREST_LIB}
    ${WORKFLOW_LIB}
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

# 把 config/ 和 uploads/ 复制到构建目录，方便本地运行
add_custom_command(TARGET webdisk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:webdisk>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/static $<TARGET_FILE_DIR:webdisk>/static
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:webdisk>/uploads
)

# ---- backup_worker 可执行文件 ----
add_executable(backup_worker
    src/backup_worker.cpp
)

target_include_directories(backup_worker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(backup_worker PRIVATE
    ${SIMPLEAMQP_LIB}
    ${RABBITMQ_LIB}
    nlohmann_json::nlohmann_json
    Threads::Threads
    pthread
)