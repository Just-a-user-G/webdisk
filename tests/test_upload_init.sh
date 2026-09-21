#!/bin/bash
# 分片上传 init 接口回归测试脚本
# 用法: bash tests/test_upload_init.sh [username] [password]

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

# 1. 登录
echo -e "${YELLOW}==> 登录 $USERNAME${NC}"
LOGIN_RESP=$(curl -s -X POST "$BASE/user/signin" \
    -d "username=$USERNAME&password=$PASSWORD")

TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['Token'])" 2>/dev/null || true)

if [ -z "$TOKEN" ]; then
    echo -e "${RED}登录失败，响应：$LOGIN_RESP${NC}"
    exit 1
fi
echo "  Token: ${TOKEN:0:16}..."
echo

INIT() {
    # INIT <form-data>
    curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" -d "$1"
}

code() {
    python3 -c "import sys,json; print(json.load(sys.stdin)['code'])"
}

field() {
    # field <json-key-path>
    python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)"
}

# 2. 正常 init
echo -e "${YELLOW}==> 正常 init${NC}"
RESP=$(INIT 'filename=movie.mp4&total_chunks=5&total_size=5242880')
CODE=$(echo "$RESP" | code)
check "正常 init 返回 code=0" "$CODE" "0"

UPLOAD_ID=$(echo "$RESP" | field "['data']['UploadID']")
TOTAL_CHUNKS=$(echo "$RESP" | field "['data']['TotalChunks']")
TOTAL_SIZE=$(echo "$RESP" | field "['data']['TotalSize']")
check "返回 UploadID 长度 = 64" "${#UPLOAD_ID}" "64"
check "返回 TotalChunks = 5" "$TOTAL_CHUNKS" "5"
check "返回 TotalSize = 5242880" "$TOTAL_SIZE" "5242880"

# 3. 临时目录已创建
if [ -n "$UPLOAD_ID" ]; then
    TMP_DIR="$(pwd)/build/uploads/tmp/$UPLOAD_ID"
    if [ -d "$TMP_DIR" ]; then
        check "临时目录已创建" "yes" "yes"
    else
        check "临时目录已创建（路径 $TMP_DIR）" "no" "yes"
    fi
fi
echo

# 4. 参数校验
echo -e "${YELLOW}==> 参数校验${NC}"

MISSING=$(INIT 'filename=')
MISSING_CODE=$(echo "$MISSING" | code)
check "缺参数返回 code=1" "$MISSING_CODE" "1"

ZERO_CHUNKS=$(INIT 'filename=x.bin&total_chunks=0&total_size=100')
ZERO_CHUNKS_CODE=$(echo "$ZERO_CHUNKS" | code)
check "total_chunks=0 返回 code=1" "$ZERO_CHUNKS_CODE" "1"

BIG_CHUNKS=$(INIT 'filename=x.bin&total_chunks=99999&total_size=100')
BIG_CHUNKS_CODE=$(echo "$BIG_CHUNKS" | code)
check "total_chunks=99999 返回 code=1" "$BIG_CHUNKS_CODE" "1"

ZERO_SIZE=$(INIT 'filename=x.bin&total_chunks=1&total_size=0')
ZERO_SIZE_CODE=$(echo "$ZERO_SIZE" | code)
check "total_size=0 返回 code=1" "$ZERO_SIZE_CODE" "1"

HUGE_SIZE=$(INIT 'filename=x.bin&total_chunks=1&total_size=99999999999')
HUGE_SIZE_CODE=$(echo "$HUGE_SIZE" | code)
check "total_size 超过 5GB 返回 code=1" "$HUGE_SIZE_CODE" "1"

BAD_NUM=$(INIT 'filename=x.bin&total_chunks=abc&total_size=100')
BAD_NUM_CODE=$(echo "$BAD_NUM" | code)
check "total_chunks=abc 返回 code=1" "$BAD_NUM_CODE" "1"

LONG_NAME=$(python3 -c "print('a'*300)")
LONG_NAME_RESP=$(INIT "filename=$LONG_NAME&total_chunks=1&total_size=100")
LONG_NAME_CODE=$(echo "$LONG_NAME_RESP" | code)
check "filename 超过 255 返回 code=1" "$LONG_NAME_CODE" "1"
echo

# 5. 无效 token
echo -e "${YELLOW}==> Token 校验${NC}"
BAD_TOKEN=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=badtoken" \
    -d 'filename=x.bin&total_chunks=1&total_size=100')
BAD_TOKEN_CODE=$(echo "$BAD_TOKEN" | code)
check "无效 token 返回 code=1" "$BAD_TOKEN_CODE" "1"

NO_TOKEN=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME" \
    -d 'filename=x.bin&total_chunks=1&total_size=100')
NO_TOKEN_CODE=$(echo "$NO_TOKEN" | code)
check "缺 token 返回 code=1" "$NO_TOKEN_CODE" "1"
echo

# 6. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
