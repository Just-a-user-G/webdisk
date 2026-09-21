#!/bin/bash
# 分片上传幂等性测试脚本
# 用法: bash tests/test_idempotent.sh [username] [password] [mysql_root_password]

set -e

BASE="http://127.0.0.1:8888"
USERNAME="${1:-alice}"
PASSWORD="${2:-123456}"
MYSQL_PASS="${3:-}"

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

mysql_exec() {
    if [ -n "$MYSQL_PASS" ]; then
        mysql -u root -p"$MYSQL_PASS" -N -B -e "use webdisk; $1"
    else
        mysql -u root -N -B -e "use webdisk; $1"
    fi
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
printf 'aaa' > /tmp/idem0
printf 'bbb' > /tmp/idem1
printf 'ccc' > /tmp/idem2

# 3. init
echo -e "${YELLOW}==> init 会话${NC}"
INIT=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=idempotent.bin&total_chunks=3&total_size=9')
UPLOAD_ID=$(echo "$INIT" | field "['data']['UploadID']")
check "init 成功" "${#UPLOAD_ID}" "64"
echo "  UploadID: $UPLOAD_ID"
echo

CHUNK() {
    curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=$1" \
        -F "chunk=@$2"
}

get_recv() {
    mysql_exec "SELECT received_chunks FROM tbl_upload_session WHERE id='$UPLOAD_ID';"
}

# 4. 初始 received_chunks = 0
echo -e "${YELLOW}==> 初始状态${NC}"
check "初始 received_chunks = 0" "$(get_recv)" "0"
echo

# 5. 上传分片 0 一次
echo -e "${YELLOW}==> 上传分片 0（第 1 次）${NC}"
CHUNK 0 /tmp/idem0 > /dev/null
check "received_chunks = 1" "$(get_recv)" "1"
echo

# 6. 重复上传分片 0（幂等测试）
echo -e "${YELLOW}==> 重复上传分片 0（第 2 次）${NC}"
CHUNK 0 /tmp/idem0 > /dev/null
check "received_chunks 仍为 1（幂等）" "$(get_recv)" "1"
echo

# 7. 再重复一次
echo -e "${YELLOW}==> 重复上传分片 0（第 3 次）${NC}"
CHUNK 0 /tmp/idem0 > /dev/null
check "received_chunks 仍为 1（幂等）" "$(get_recv)" "1"
echo

# 8. 上传分片 1
echo -e "${YELLOW}==> 上传分片 1${NC}"
CHUNK 1 /tmp/idem1 > /dev/null
check "received_chunks = 2" "$(get_recv)" "2"
echo

# 9. 乱序重传分片 0
echo -e "${YELLOW}==> 乱序重传分片 0${NC}"
CHUNK 0 /tmp/idem0 > /dev/null
check "received_chunks 仍为 2（乱序幂等）" "$(get_recv)" "2"
echo

# 10. 上传分片 2
echo -e "${YELLOW}==> 上传分片 2${NC}"
CHUNK 2 /tmp/idem2 > /dev/null
check "received_chunks = 3" "$(get_recv)" "3"
echo

# 11. complete 并校验内容
echo -e "${YELLOW}==> complete 合并${NC}"
COMPLETE=$(curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID")
COMPLETE_CODE=$(echo "$COMPLETE" | field "['code']")
check "complete 返回 code=0" "$COMPLETE_CODE" "0"

HASH=$(echo "$COMPLETE" | field "['data']['FileHash']")
echo "  FileHash: $HASH"

# 下载校验
curl -s -G "$BASE/file/download" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN" \
    --data-urlencode "filename=idempotent.bin" \
    --data-urlencode "filehash=$HASH" \
    -o /tmp/idem_merged.bin

ACTUAL=$(cat /tmp/idem_merged.bin)
check "合并内容 = 'aaabbbccc'" "$ACTUAL" "aaabbbccc"
echo

# 12. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
