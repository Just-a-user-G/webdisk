#!/bin/bash
# 分片上传状态查询接口测试脚本
# 用法: bash tests/test_upload_status.sh [username] [password]

set -e

BASE="http://127.0.0.1:8888"
USERNAME="${1:-alice}"
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

# 1. 登录
echo -e "${YELLOW}==> 登录 $USERNAME${NC}"
LOGIN=$(curl -s -X POST "$BASE/user/signin" -d "username=$USERNAME&password=$PASSWORD")
TOKEN=$(echo "$LOGIN" | field "['data']['Token']")
if [ -z "$TOKEN" ] || [ "$TOKEN" = "PARSE_ERR" ]; then
    echo -e "${RED}登录失败：$LOGIN${NC}"
    exit 1
fi
echo "  Token: ${TOKEN:0:16}..."
echo

# 2. 准备分片数据
printf 'aaa' > /tmp/st0
printf 'bbb' > /tmp/st1
printf 'ccc' > /tmp/st2
printf 'ddd' > /tmp/st3

# 3. init 一个 4 片的会话
echo -e "${YELLOW}==> init 会话（4 片）${NC}"
INIT=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=status_test.bin&total_chunks=4&total_size=12')
UPLOAD_ID=$(echo "$INIT" | field "['data']['UploadID']")
check "init 成功" "${#UPLOAD_ID}" "64"
echo "  UploadID: $UPLOAD_ID"
echo

STATUS() {
    curl -s -G "$BASE/file/upload/status" \
        --data-urlencode "username=$USERNAME" \
        --data-urlencode "token=$TOKEN" \
        --data-urlencode "upload_id=$1"
}

CHUNK() {
    curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=$1" \
        -F "chunk=@$2" > /dev/null
}

received_arr() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(','.join(map(str, d['data']['ReceivedChunks'])))"
}

# 4. 初始状态：无分片
echo -e "${YELLOW}==> 初始状态${NC}"
S0=$(STATUS "$UPLOAD_ID")
CHECK_CODE=$(echo "$S0" | field "['code']")
check "status 返回 code=0" "$CHECK_CODE" "0"

FNAME=$(echo "$S0" | field "['data']['FileName']")
check "FileName = status_test.bin" "$FNAME" "status_test.bin"

TOTAL=$(echo "$S0" | field "['data']['TotalChunks']")
check "TotalChunks = 4" "$TOTAL" "4"

RECV=$(echo "$S0" | received_arr)
check "初始 ReceivedChunks 为空" "$RECV" ""
echo

# 5. 传分片 0
echo -e "${YELLOW}==> 传分片 0${NC}"
CHUNK 0 /tmp/st0
RECV=$(STATUS "$UPLOAD_ID" | received_arr)
check "ReceivedChunks = [0]" "$RECV" "0"
echo

# 6. 传分片 2
echo -e "${YELLOW}==> 传分片 2${NC}"
CHUNK 2 /tmp/st2
RECV=$(STATUS "$UPLOAD_ID" | received_arr)
check "ReceivedChunks = [0,2]" "$RECV" "0,2"
echo

# 7. 传分片 1
echo -e "${YELLOW}==> 传分片 1${NC}"
CHUNK 1 /tmp/st1
RECV=$(STATUS "$UPLOAD_ID" | received_arr)
check "ReceivedChunks = [0,1,2]" "$RECV" "0,1,2"
echo

# 8. 重复传分片 0，状态不变
echo -e "${YELLOW}==> 重复传分片 0${NC}"
CHUNK 0 /tmp/st0
RECV=$(STATUS "$UPLOAD_ID" | received_arr)
check "ReceivedChunks 仍为 [0,1,2]" "$RECV" "0,1,2"
echo

# 9. 传分片 3，全齐
echo -e "${YELLOW}==> 传分片 3${NC}"
CHUNK 3 /tmp/st3
RECV=$(STATUS "$UPLOAD_ID" | received_arr)
check "ReceivedChunks = [0,1,2,3]" "$RECV" "0,1,2,3"
echo

# 10. complete 后再查，应该报错（会话已不是 status=0）
echo -e "${YELLOW}==> complete 后再查状态${NC}"
curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID" > /dev/null

AFTER=$(STATUS "$UPLOAD_ID")
AFTER_CODE=$(echo "$AFTER" | field "['code']")
check "完成后的会话查状态返回 code=1" "$AFTER_CODE" "1"
echo

# 11. 错误场景
echo -e "${YELLOW}==> 错误场景${NC}"

# 缺 upload_id
NO_UID=$(curl -s -G "$BASE/file/upload/status" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN")
NO_UID_CODE=$(echo "$NO_UID" | field "['code']")
check "缺 upload_id 返回 code=1" "$NO_UID_CODE" "1"

# 无效 upload_id
BAD_UID=$(STATUS "notexist")
BAD_UID_CODE=$(echo "$BAD_UID" | field "['code']")
check "无效 upload_id 返回 code=1" "$BAD_UID_CODE" "1"

# 无效 token
BAD_TOKEN=$(curl -s -G "$BASE/file/upload/status" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=bad" \
    --data-urlencode "upload_id=$UPLOAD_ID")
BAD_TOKEN_CODE=$(echo "$BAD_TOKEN" | field "['code']")
check "无效 token 返回 code=1" "$BAD_TOKEN_CODE" "1"
echo

# 12. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
