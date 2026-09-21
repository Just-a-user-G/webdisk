#!/bin/bash
# 文件搜索接口回归测试脚本
# 用法: bash tests/test_search.sh [username] [password]

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

check_ge() {
    local desc="$1"
    local actual="$2"
    local min="$3"
    if [ "$actual" -ge "$min" ]; then
        echo -e "${GREEN}[PASS]${NC} $desc"
        pass=$((pass+1))
    else
        echo -e "${RED}[FAIL]${NC} $desc (期望 >= $min，实际 $actual)"
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

Q() {
    # Q <form-data>
    curl -s -X POST "$BASE/file/query?username=$USERNAME&token=$TOKEN" -d "$1"
}

count() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['data']))"
}

code() {
    python3 -c "import sys,json; print(json.load(sys.stdin)['code'])"
}

# 2. 不带 keyword，取总数
echo -e "${YELLOW}==> 基础测试${NC}"
ALL=$(Q 'limit=1000')
TOTAL=$(echo "$ALL" | count)
echo "  文件总数：$TOTAL"
echo

# 3. 搜索测试
echo -e "${YELLOW}==> 搜索测试${NC}"

# 3.1 空 keyword 等价于不搜索
EMPTY_KW=$(Q 'limit=1000&keyword=')
EMPTY_KW_COUNT=$(echo "$EMPTY_KW" | count)
check "空 keyword 返回全部（$EMPTY_KW_COUNT = $TOTAL）" "$EMPTY_KW_COUNT" "$TOTAL"

# 3.2 匹配 .txt
TXT=$(Q 'limit=1000&keyword=.txt')
TXT_COUNT=$(echo "$TXT" | count)
check_ge "搜 .txt 至少返回 0 条" "$TXT_COUNT" 0

# 3.3 结果中的所有文件名都包含关键字
if [ "$TXT_COUNT" -gt 0 ]; then
    ALL_MATCH=$(echo "$TXT" | python3 -c "
import sys,json
d=json.load(sys.stdin)['data']
print('yes' if all('.txt' in f['FileName'] for f in d) else 'no')
")
    check "返回的文件名都包含 '.txt'" "$ALL_MATCH" "yes"
fi

# 3.4 搜不存在的关键字返回空
NONE=$(Q 'limit=1000&keyword=zzzznotexist_xyz')
NONE_COUNT=$(echo "$NONE" | count)
check "搜不存在的关键字返回 0 条" "$NONE_COUNT" "0"

# 3.5 SQL 注入防护
INJECT=$(Q "limit=1000&keyword=' OR 1=1 --")
INJECT_COUNT=$(echo "$INJECT" | count)
check "SQL 注入不会返回全部文件（应为 0）" "$INJECT_COUNT" "0"

# 3.6 keyword 超长
LONG_KW=$(python3 -c "print('a'*300)")
LONG=$(Q "limit=1000&keyword=$LONG_KW")
LONG_CODE=$(echo "$LONG" | code)
check "超长 keyword 返回 code=1" "$LONG_CODE" "1"

# 3.7 搜索 + 分页
SP1=$(Q 'limit=1&offset=0&keyword=.txt')
SP1_COUNT=$(echo "$SP1" | count)
check "搜索+分页第 1 页 <= 1 条" "$([ $SP1_COUNT -le 1 ] && echo yes || echo no)" "yes"

SP2=$(Q 'limit=1&offset=1&keyword=.txt')
SP2_COUNT=$(echo "$SP2" | count)
check "搜索+分页第 2 页 <= 1 条" "$([ $SP2_COUNT -le 1 ] && echo yes || echo no)" "yes"

# 4. 汇总
echo
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
