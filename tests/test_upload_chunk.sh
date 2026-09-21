#!/bin/bash
# 分片上传 chunk 接口回归测试脚本
# 用法: bash tests/test_upload_chunk.sh [username] [password]

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

code() {
    python3 -c "import sys,json; print(json.load(sys.stdin)['code'])"
}

field() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)"
}

# 2. 准备临时数据
echo -e "${YELLOW}==> 准备分片文件${NC}"
printf 'hello' > /tmp/chunk0
printf 'world' > /tmp/chunk1
printf 'xyz'   > /tmp/chunk2
echo "  /tmp/chunk0 = 'hello' (5B)"
echo "  /tmp/chunk1 = 'world' (5B)"
echo "  /tmp/chunk2 = 'xyz' (3B)"
echo

# 3. init 一个 2 片的会话
echo -e "${YELLOW}==> init 会话${NC}"
INIT_RESP=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=test.bin&total_chunks=2&total_size=10')
UPLOAD_ID=$(echo "$INIT_RESP" | field "['data']['UploadID']")
check "init 返回 64 位 UploadID" "${#UPLOAD_ID}" "64"
echo "  UploadID: $UPLOAD_ID"
echo

CHUNK() {
    # CHUNK <upload_id> <index> <file>
    curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$1&chunk_index=$2" \
        -F "chunk=@$3"
}

# 4. 上传第 0 片
echo -e "${YELLOW}==> 上传分片${NC}"
R0=$(CHUNK "$UPLOAD_ID" 0 /tmp/chunk0)
CODE0=$(echo "$R0" | code)
CHUNK_SIZE0=$(echo "$R0" | field "['data']['ChunkSize']")
check "上传分片 0 成功" "$CODE0" "0"
check "分片 0 大小 = 5" "$CHUNK_SIZE0" "5"

# 5. 上传第 1 片
R1=$(CHUNK "$UPLOAD_ID" 1 /tmp/chunk1)
CODE1=$(echo "$R1" | code)
check "上传分片 1 成功" "$CODE1" "0"
echo

# 6. 分片文件已落盘
echo -e "${YELLOW}==> 检查落盘${NC}"
CHUNK_DIR="$(pwd)/build/uploads/tmp/$UPLOAD_ID"
if [ -f "$CHUNK_DIR/0" ] && [ -f "$CHUNK_DIR/1" ]; then
    check "两个分片文件都存在" "yes" "yes"
    SIZE0=$(stat -c%s "$CHUNK_DIR/0")
    SIZE1=$(stat -c%s "$CHUNK_DIR/1")
    check "分片 0 文件大小 = 5" "$SIZE0" "5"
    check "分片 1 文件大小 = 5" "$SIZE1" "5"
else
    check "两个分片文件都存在（目录 $CHUNK_DIR）" "no" "yes"
fi
echo

# 7. 参数校验
echo -e "${YELLOW}==> 参数校验${NC}"

# 缺 upload_id
NO_UID=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&chunk_index=0" \
    -F "chunk=@/tmp/chunk0")
NO_UID_CODE=$(echo "$NO_UID" | code)
check "缺 upload_id 返回 code=1" "$NO_UID_CODE" "1"

# 无效 upload_id
BAD_UID=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=notexist&chunk_index=0" \
    -F "chunk=@/tmp/chunk0")
BAD_UID_CODE=$(echo "$BAD_UID" | code)
check "无效 upload_id 返回 code=1" "$BAD_UID_CODE" "1"

# 非法 chunk_index
BAD_IDX=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=abc" \
    -F "chunk=@/tmp/chunk0")
BAD_IDX_CODE=$(echo "$BAD_IDX" | code)
check "非法 chunk_index 返回 code=1" "$BAD_IDX_CODE" "1"

# 越界 chunk_index
OOR_IDX=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=99" \
    -F "chunk=@/tmp/chunk0")
OOR_IDX_CODE=$(echo "$OOR_IDX" | code)
check "越界 chunk_index 返回 code=1" "$OOR_IDX_CODE" "1"

# 缺 chunk 字段
NO_CHUNK=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$UPLOAD_ID&chunk_index=0")
NO_CHUNK_CODE=$(echo "$NO_CHUNK" | code)
check "缺 chunk 字段返回 code=1" "$NO_CHUNK_CODE" "1"

# 无效 token
BAD_TOKEN=$(curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=bad&upload_id=$UPLOAD_ID&chunk_index=0" \
    -F "chunk=@/tmp/chunk0")
BAD_TOKEN_CODE=$(echo "$BAD_TOKEN" | code)
check "无效 token 返回 code=1" "$BAD_TOKEN_CODE" "1"
echo

# 8. 数据库 received_chunks 计数
echo -e "${YELLOW}==> 数据库计数${NC}"
RECV=$(mysql -u root -p -N -B -e \
    "use webdisk; SELECT received_chunks FROM tbl_upload_session WHERE id='$UPLOAD_ID';" 2>/dev/null || echo "ERR")
if [ "$RECV" = "2" ]; then
    check "received_chunks = 2" "yes" "yes"
else
    check "received_chunks = 2（实际 $RECV）" "no" "yes"
fi
echo

# 9. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
