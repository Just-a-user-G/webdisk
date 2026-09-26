#!/bin/bash
# 第四期微服务完整链路测试
# 网关 → 用户服务 → 文件服务
# 用法: bash tests/test_microservices.sh [username] [password]

set -e

BASE="http://127.0.0.1:8888"
USERNAME="${1:-microtest}"
PASSWORD="${2:-123456}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass=0
fail=0

check() {
    local desc="$1"
    local actual="$2"
    local expected="$3"
    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}[PASS]${NC} $desc"
        pass=$((pass+1))
    else
        echo -e "${RED}[FAIL]${NC} $desc"
        echo "  期望: $expected"
        echo "  实际: $actual"
        fail=$((fail+1))
    fi
}

field() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)" 2>/dev/null || echo "PARSE_ERR"
}

# 0. 前置检查：三个端口
echo -e "${YELLOW}==> 检查服务端口${NC}"
for port in 9001 9002 8888; do
    if sudo ss -ltn 2>/dev/null | grep -q ":$port "; then
        check "端口 $port 在监听" "yes" "yes"
    else
        check "端口 $port 在监听" "no" "yes"
    fi
done
echo

# 1. 注册（忽略重名错误）
echo -e "${YELLOW}==> 注册 $USERNAME${NC}"
REG=$(curl -s -X POST "$BASE/user/signup" -d "username=$USERNAME&password=$PASSWORD")
REG_CODE=$(echo "$REG" | field "['code']")
if [ "$REG_CODE" = "0" ]; then
    check "注册成功" "yes" "yes"
else
    REG_MSG=$(echo "$REG" | field "['msg']")
    if echo "$REG_MSG" | grep -q "already exists"; then
        check "用户已存在（视为通过）" "yes" "yes"
    else
        check "注册返回意外错误：$REG_MSG" "no" "yes"
    fi
fi
echo

# 2. 登录
echo -e "${YELLOW}==> 登录 $USERNAME${NC}"
LOGIN=$(curl -s -X POST "$BASE/user/signin" -d "username=$USERNAME&password=$PASSWORD")
TOKEN=$(echo "$LOGIN" | field "['data']['Token']")
check "登录返回 Token 长度 = 64" "${#TOKEN}" "64"
echo "  Token: ${TOKEN:0:16}..."
echo

# 3. 用户信息
echo -e "${YELLOW}==> 用户信息${NC}"
INFO=$(curl -s -G "$BASE/user/info" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN")
INFO_CODE=$(echo "$INFO" | field "['code']")
INFO_NAME=$(echo "$INFO" | field "['data']['Username']")
check "info 返回 code=0" "$INFO_CODE" "0"
check "info 返回用户名匹配" "$INFO_NAME" "$USERNAME"
echo

# 4. 文件列表（先记录初始条数）
echo -e "${YELLOW}==> 文件列表${NC}"
LIST0=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" -d 'limit=100')
LIST0_CODE=$(echo "$LIST0" | field "['code']")
LIST0_COUNT=$(echo "$LIST0" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))" 2>/dev/null || echo 0)
check "列表返回 code=0" "$LIST0_CODE" "0"
echo "  初始文件数: $LIST0_COUNT"
echo

# 5. 普通上传
echo -e "${YELLOW}==> 普通上传${NC}"
TEST_FILE="/tmp/micro_test_$$.txt"
echo "microservices test $(date +%s)" > "$TEST_FILE"
UP=$(curl -s -X POST "$BASE/file/upload?username=$USERNAME&token=$TOKEN" \
    -F "file=@$TEST_FILE")
UP_CODE=$(echo "$UP" | field "['code']")
UP_HASH=$(echo "$UP" | field "['data']['FileHash']")
UP_NAME=$(echo "$UP" | field "['data']['FileName']")
check "上传返回 code=0" "$UP_CODE" "0"
check "FileHash 长度 = 64" "${#UP_HASH}" "64"
echo "  FileName: $UP_NAME"
echo "  FileHash: $UP_HASH"
echo

# 6. 列表里能找到
echo -e "${YELLOW}==> 列表确认${NC}"
LIST1=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" -d "limit=100&keyword=$UP_NAME")
LIST1_COUNT=$(echo "$LIST1" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))" 2>/dev/null || echo 0)
check "上传后列表能查到" "$([ "$LIST1_COUNT" -ge 1 ] && echo yes || echo no)" "yes"
echo

# 7. 下载并校验内容
echo -e "${YELLOW}==> 下载校验${NC}"
curl -s -G "$BASE/file/download" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN" \
    --data-urlencode "filename=$UP_NAME" \
    --data-urlencode "filehash=$UP_HASH" \
    -o /tmp/micro_downloaded_$$.txt
if diff -q "$TEST_FILE" /tmp/micro_downloaded_$$.txt > /dev/null; then
    check "下载内容一致" "yes" "yes"
else
    check "下载内容一致" "no" "yes"
fi
echo

# 8. 重命名
echo -e "${YELLOW}==> 重命名${NC}"
NEW_NAME="renamed_$$.txt"
RENAME=$(curl -s -X POST "$BASE/file/rename?username=$USERNAME&token=$TOKEN" \
    -d "old_filename=$UP_NAME&new_filename=$NEW_NAME&filehash=$UP_HASH")
RENAME_CODE=$(echo "$RENAME" | field "['code']")
check "重命名返回 code=0" "$RENAME_CODE" "0"

# 确认列表里是新名字
LIST2=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" -d "limit=100&keyword=$NEW_NAME")
LIST2_COUNT=$(echo "$LIST2" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))" 2>/dev/null || echo 0)
check "列表能查到新名字" "$([ "$LIST2_COUNT" -ge 1 ] && echo yes || echo no)" "yes"
echo

# 9. 删除
echo -e "${YELLOW}==> 删除${NC}"
DEL=$(curl -s -X POST "$BASE/file/delete?username=$USERNAME&token=$TOKEN" \
    -d "filename=$NEW_NAME&filehash=$UP_HASH")
DEL_CODE=$(echo "$DEL" | field "['code']")
check "删除返回 code=0" "$DEL_CODE" "0"

LIST3=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" -d "limit=100&keyword=$NEW_NAME")
LIST3_COUNT=$(echo "$LIST3" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))" 2>/dev/null || echo 0)
check "删除后列表查不到" "$LIST3_COUNT" "0"
echo

# 10. 分片上传完整流程
echo -e "${YELLOW}==> 分片上传${NC}"
printf 'hello' > /tmp/mc_chunk0
printf 'world' > /tmp/mc_chunk1

INIT=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=micro_merge.bin&total_chunks=2&total_size=10')
UPLOAD_ID=$(echo "$INIT" | field "['data']['UploadID']")
check "init 返回 64 位 UploadID" "${#UPLOAD_ID}" "64"

# 传分片 1 再传分片 0（乱序）
curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=1" \
    -F 'chunk=@/tmp/mc_chunk1' > /dev/null
curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=0" \
    -F 'chunk=@/tmp/mc_chunk0' > /dev/null

STATUS=$(curl -s -G "$BASE/file/upload/status" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN" \
    --data-urlencode "upload_id=$UPLOAD_ID")
RECV=$(echo "$STATUS" | python3 -c "import sys,json; d=json.load(sys.stdin); print(','.join(map(str, d['data']['ReceivedChunks'])))" 2>/dev/null || echo "")
check "已收到分片 [0,1]" "$RECV" "0,1"

COMPLETE=$(curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID")
COMPLETE_CODE=$(echo "$COMPLETE" | field "['code']")
COMPLETE_HASH=$(echo "$COMPLETE" | field "['data']['FileHash']")
check "complete 返回 code=0" "$COMPLETE_CODE" "0"
check "complete 返回 FileHash 长度 = 64" "${#COMPLETE_HASH}" "64"

# 下载合并结果，校验内容
curl -s -G "$BASE/file/download" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN" \
    --data-urlencode "filename=micro_merge.bin" \
    --data-urlencode "filehash=$COMPLETE_HASH" \
    -o /tmp/micro_merged_$$.bin
ACTUAL=$(cat /tmp/micro_merged_$$.bin)
check "合并内容 = 'helloworld'" "$ACTUAL" "helloworld"

# 清理分片上传产生的文件
curl -s -X POST "$BASE/file/delete?username=$USERNAME&token=$TOKEN" \
    -d "filename=micro_merge.bin&filehash=$COMPLETE_HASH" > /dev/null
echo

# 11. 错误场景
echo -e "${YELLOW}==> 错误场景${NC}"

BAD_TOKEN=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=bad" -d 'limit=10')
BAD_TOKEN_CODE=$(echo "$BAD_TOKEN" | field "['code']")
check "无效 token 返回 code=1" "$BAD_TOKEN_CODE" "1"

NO_USER=$(curl -s -X POST "$BASE/user/signin" -d 'username=nobody_xyz&password=x')
NO_USER_CODE=$(echo "$NO_USER" | field "['code']")
check "登录不存在的用户返回 code=1" "$NO_USER_CODE" "1"

NOT_FOUND=$(curl -s -X POST "$BASE/file/delete?username=$USERNAME&token=$TOKEN" \
    -d "filename=notexist.bin&filehash=0000")
NOT_FOUND_CODE=$(echo "$NOT_FOUND" | field "['code']")
check "删除不存在的文件返回 code=1" "$NOT_FOUND_CODE" "1"
echo

# 清理临时文件
rm -f "$TEST_FILE" "/tmp/micro_downloaded_$$.txt" "/tmp/micro_merged_$$.bin" \
      /tmp/mc_chunk0 /tmp/mc_chunk1

# 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
