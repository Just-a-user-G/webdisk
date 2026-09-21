#!/bin/bash
# 分片上传完整流程测试脚本（init + chunk + complete）
# 用法: bash tests/test_upload_complete.sh [username] [password]

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

code() {
    python3 -c "import sys,json; print(json.load(sys.stdin)['code'])" 2>/dev/null || echo "PARSE_ERR"
}

field() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)" 2>/dev/null || echo "PARSE_ERR"
}

# 1. 登录
echo -e "${YELLOW}==> 登录 $USERNAME${NC}"
LOGIN_RESP=$(curl -s -X POST "$BASE/user/signin" \
    -d "username=$USERNAME&password=$PASSWORD")
TOKEN=$(echo "$LOGIN_RESP" | field "['data']['Token']")
if [ "$TOKEN" = "PARSE_ERR" ] || [ -z "$TOKEN" ]; then
    echo -e "${RED}登录失败：$LOGIN_RESP${NC}"
    exit 1
fi
echo "  Token: ${TOKEN:0:16}..."
echo

# 2. 准备分片数据
echo -e "${YELLOW}==> 准备分片文件${NC}"
printf 'hello' > /tmp/chunk0
printf 'world' > /tmp/chunk1
printf '!!!'   > /tmp/chunk2
echo "  chunk0='hello' (5B), chunk1='world' (5B), chunk2='!!!' (3B)"
echo

# 3. init
echo -e "${YELLOW}==> init 会话${NC}"
INIT=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=test_merge.bin&total_chunks=3&total_size=13')
UPLOAD_ID=$(echo "$INIT" | field "['data']['UploadID']")
check "init 返回 64 位 UploadID" "${#UPLOAD_ID}" "64"
echo "  UploadID: $UPLOAD_ID"
echo

CHUNK() {
    # CHUNK <index> <file>
    curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=$1" \
        -F "chunk=@$2"
}

COMPLETE() {
    curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN&upload_id=$1"
}

# 4. 还没传完就 complete，应报错
echo -e "${YELLOW}==> 提前 complete（应失败）${NC}"
EARLY=$(COMPLETE "$UPLOAD_ID")
EARLY_CODE=$(echo "$EARLY" | code)
check "分片未收齐时 complete 返回 code=1" "$EARLY_CODE" "1"
echo

# 5. 上传所有分片
echo -e "${YELLOW}==> 上传分片${NC}"
for i in 0 1 2; do
    R=$(CHUNK $i "/tmp/chunk$i")
    C=$(echo "$R" | code)
    check "上传分片 $i" "$C" "0"
done
echo

# 6. complete
echo -e "${YELLOW}==> complete 合并${NC}"
RESP=$(COMPLETE "$UPLOAD_ID")
CODE=$(echo "$RESP" | code)
check "complete 返回 code=0" "$CODE" "0"

HASH=$(echo "$RESP" | field "['data']['FileHash']")
FNAME=$(echo "$RESP" | field "['data']['FileName']")
FSIZE=$(echo "$RESP" | field "['data']['FileSize']")
check "FileHash 长度 = 64" "${#HASH}" "64"
check "FileName = test_merge.bin" "$FNAME" "test_merge.bin"
check "FileSize = 13" "$FSIZE" "13"
echo

# 7. 临时目录被清理
echo -e "${YELLOW}==> 临时目录清理${NC}"
TMP_DIR="$(pwd)/build/uploads/tmp/$UPLOAD_ID"
if [ -d "$TMP_DIR" ]; then
    check "临时目录已删除" "no" "yes"
else
    check "临时目录已删除" "yes" "yes"
fi
echo

# 8. 重复 complete 应失败（会话已 status=1）
echo -e "${YELLOW}==> 重复 complete${NC}"
AGAIN=$(COMPLETE "$UPLOAD_ID")
AGAIN_CODE=$(echo "$AGAIN" | code)
check "重复 complete 返回 code=1" "$AGAIN_CODE" "1"
echo

# 9. 文件列表里能查到
echo -e "${YELLOW}==> 文件列表验证${NC}"
LIST=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=100&keyword=test_merge.bin')
LIST_CODE=$(echo "$LIST" | code)
check "查询返回 code=0" "$LIST_CODE" "0"

FOUND=$(echo "$LIST" | python3 -c "
import sys, json
d = json.load(sys.stdin)
items = [f for f in d['data'] if f['FileName'] == 'test_merge.bin']
print('yes' if items else 'no')
")
check "列表里能找到 test_merge.bin" "$FOUND" "yes"
echo

# 10. 下载并校验内容
echo -e "${YELLOW}==> 下载校验${NC}"
curl -s -G "$BASE/file/download" \
    --data-urlencode "username=$USERNAME" \
    --data-urlencode "token=$TOKEN" \
    --data-urlencode "filename=test_merge.bin" \
    --data-urlencode "filehash=$HASH" \
    -o /tmp/merged.bin

EXPECTED="helloworld!!!"
ACTUAL=$(cat /tmp/merged.bin)
check "下载内容 = '$EXPECTED'" "$ACTUAL" "$EXPECTED"
echo

# 11. 错误场景
echo -e "${YELLOW}==> 错误场景${NC}"

# 缺 upload_id
NO_UID=$(curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN")
NO_UID_CODE=$(echo "$NO_UID" | code)
check "缺 upload_id 返回 code=1" "$NO_UID_CODE" "1"

# 无效 upload_id
BAD_UID=$(COMPLETE "notexist123")
BAD_UID_CODE=$(echo "$BAD_UID" | code)
check "无效 upload_id 返回 code=1" "$BAD_UID_CODE" "1"

# 无效 token
BAD_TOKEN=$(curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=bad&upload_id=$UPLOAD_ID")
BAD_TOKEN_CODE=$(echo "$BAD_TOKEN" | code)
check "无效 token 返回 code=1" "$BAD_TOKEN_CODE" "1"
echo

# 12. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
