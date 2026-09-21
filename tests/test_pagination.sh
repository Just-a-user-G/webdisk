#!/bin/bash
# 分页接口回归测试脚本
# 用法: bash tests/test_pagination.sh [username] [password]

set -e

BASE="http://127.0.0.1:8888"
USERNAME="${1:-alice}"
PASSWORD="${2:-123456}"

# 颜色
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

# 1. 登录拿 token
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

# 2. 查总数
echo -e "${YELLOW}==> 查询全部文件${NC}"
ALL=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=1000')
TOTAL=$(echo "$ALL" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))")
echo "  共 $TOTAL 条文件"
echo

# 3. 分页测试
echo -e "${YELLOW}==> 分页测试${NC}"

# 3.1 第 1 页
P1=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=2&offset=0')
P1_COUNT=$(echo "$P1" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))")
check "第 1 页返回条数 <= 2" "$([ $P1_COUNT -le 2 ] && echo yes || echo no)" "yes"

# 3.2 第 2 页
P2=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=2&offset=2')
P2_COUNT=$(echo "$P2" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))")
check "第 2 页返回条数 <= 2" "$([ $P2_COUNT -le 2 ] && echo yes || echo no)" "yes"

# 3.3 第 1 页和第 2 页不重叠
if [ "$P1_COUNT" -gt 0 ] && [ "$P2_COUNT" -gt 0 ]; then
    H1=$(echo "$P1" | python3 -c "import sys,json; d=json.load(sys.stdin)['data']; print(d[0]['FileHash'])")
    H2=$(echo "$P2" | python3 -c "import sys,json; d=json.load(sys.stdin)['data']; print(d[0]['FileHash'])")
    check "两页首条不重叠" "$([ "$H1" != "$H2" ] && echo yes || echo no)" "yes"
else
    echo -e "${YELLOW}[SKIP]${NC} 文件太少，跳过重叠测试"
fi

# 3.4 offset 超出范围返回空
EMPTY=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=2&offset=10000')
EMPTY_COUNT=$(echo "$EMPTY" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))")
check "offset 超出范围返回空数组" "$EMPTY_COUNT" "0"

# 3.5 非法 offset
BAD_OFF=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=2&offset=abc')
BAD_OFF_CODE=$(echo "$BAD_OFF" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
check "非法 offset 返回 code=1" "$BAD_OFF_CODE" "1"

# 3.6 非法 limit
BAD_LIM=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=xyz&offset=0')
BAD_LIM_CODE=$(echo "$BAD_LIM" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
check "非法 limit 返回 code=1" "$BAD_LIM_CODE" "1"

# 3.7 负数 offset 被夹到 0
NEG_OFF=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=2&offset=-5')
NEG_OFF_CODE=$(echo "$NEG_OFF" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
check "负数 offset 不报错（code=0）" "$NEG_OFF_CODE" "0"

# 3.8 limit 超上限被夹到 1000
BIG_LIM=$(curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" \
    -d 'limit=99999&offset=0')
BIG_LIM_CODE=$(echo "$BIG_LIM" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
check "超大 limit 不报错（code=0）" "$BIG_LIM_CODE" "0"

echo
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
