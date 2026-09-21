#!/bin/bash
# 临时目录自动清理回归测试脚本
# 用法: bash tests/test_cleanup.sh [username] [password] [mysql_root_password]

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

# MySQL 执行
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

# 2. 创建一个会话，手动改成 25 小时前
echo -e "${YELLOW}==> 创建过期会话${NC}"
INIT_OLD=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=stale.bin&total_chunks=2&total_size=100')
OLD_ID=$(echo "$INIT_OLD" | field "['data']['UploadID']")
check "创建过期会话成功" "${#OLD_ID}" "64"
echo "  旧会话 ID: $OLD_ID"

# 手动改 created_at 为 25 小时前
mysql_exec "UPDATE tbl_upload_session SET created_at = NOW() - INTERVAL 25 HOUR WHERE id='$OLD_ID';"
AGE=$(mysql_exec "SELECT TIMESTAMPDIFF(HOUR, created_at, NOW()) FROM tbl_upload_session WHERE id='$OLD_ID';")
check "旧会话 age > 24h" "$([ "$AGE" -gt 24 ] && echo yes || echo no)" "yes"
echo

# 3. 确认旧会话的临时目录存在
echo -e "${YELLOW}==> 检查旧会话临时目录${NC}"
OLD_DIR="$(pwd)/build/uploads/tmp/$OLD_ID"
if [ -d "$OLD_DIR" ]; then
    check "旧临时目录存在" "yes" "yes"
else
    check "旧临时目录存在" "no" "yes"
fi
echo

# 4. 创建一个新会话（应该触发清理）
echo -e "${YELLOW}==> 创建新会话（触发清理）${NC}"
INIT_NEW=$(curl -s -X POST "$BASE/file/upload/init?username=$USERNAME&token=$TOKEN" \
    -d 'filename=fresh.bin&total_chunks=2&total_size=100')
NEW_ID=$(echo "$INIT_NEW" | field "['data']['UploadID']")
check "创建新会话成功" "${#NEW_ID}" "64"
echo "  新会话 ID: $NEW_ID"
echo

# 5. 验证旧会话被清理
echo -e "${YELLOW}==> 验证清理结果${NC}"

OLD_DB=$(mysql_exec "SELECT COUNT(*) FROM tbl_upload_session WHERE id='$OLD_ID';")
check "旧会话数据库记录已删除" "$OLD_DB" "0"

NEW_DB=$(mysql_exec "SELECT COUNT(*) FROM tbl_upload_session WHERE id='$NEW_ID';")
check "新会话数据库记录保留" "$NEW_DB" "1"

if [ ! -d "$OLD_DIR" ]; then
    check "旧临时目录已删除" "yes" "yes"
else
    check "旧临时目录已删除" "no" "yes"
fi

NEW_DIR="$(pwd)/build/uploads/tmp/$NEW_ID"
if [ -d "$NEW_DIR" ]; then
    check "新临时目录保留" "yes" "yes"
else
    check "新临时目录保留" "no" "yes"
fi
echo

# 6. 清理测试残留
echo -e "${YELLOW}==> 清理测试残留${NC}"
curl -s -X POST "$BASE/file/upload/chunk?username=$USERNAME&token=$TOKEN&upload_id=$NEW_ID&chunk_index=0" \
    -F "chunk=@/tmp/chunk0" > /dev/null
curl -s -X POST "$BASE/file/upload/complete?username=$USERNAME&token=$TOKEN&upload_id=$NEW_ID" > /dev/null
echo "  新会话已 complete，临时目录已清理"
echo

# 7. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
