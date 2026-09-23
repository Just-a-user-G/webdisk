#!/bin/bash
# 上传后发消息到 MQ 的测试脚本
# 用法: bash tests/test_mq_publish.sh [username] [password]

set -e

BASE="http://127.0.0.1:8888"
RABBIT="http://127.0.0.1:15672"
RABBIT_AUTH="guest:guest"
QUEUE="backup.queue"
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

field() {
    python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)" 2>/dev/null || echo "PARSE_ERR"
}

queue_count() {
    curl -s -u "$RABBIT_AUTH" "$RABBIT/api/queues/%2f/$QUEUE" \
        | python3 -c "import sys,json; print(json.load(sys.stdin).get('messages', 0))" 2>/dev/null || echo "ERR"
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

# 2. 记录上传前的队列消息数
echo -e "${YELLOW}==> 记录队列初始消息数${NC}"
BEFORE=$(queue_count)
if [ "$BEFORE" = "ERR" ]; then
    echo -e "${RED}无法读取队列消息数，检查 RabbitMQ 是否在跑、队列 $QUEUE 是否存在${NC}"
    exit 1
fi
echo "  上传前 messages = $BEFORE"
echo

# 3. 上传文件
echo -e "${YELLOW}==> 上传文件${NC}"
echo "mq test $(date +%s)" > /tmp/mq_test_$$.txt
UPLOAD=$(curl -s -X POST "$BASE/file/upload?username=$USERNAME&token=$TOKEN" \
    -F "file=@/tmp/mq_test_$$.txt")
UPLOAD_CODE=$(echo "$UPLOAD" | field "['code']")
UPLOAD_HASH=$(echo "$UPLOAD" | field "['data']['FileHash']")
UPLOAD_NAME=$(echo "$UPLOAD" | field "['data']['FileName']")
check "上传返回 code=0" "$UPLOAD_CODE" "0"
check "FileHash 长度 = 64" "${#UPLOAD_HASH}" "64"
echo "  FileName: $UPLOAD_NAME"
echo "  FileHash: $UPLOAD_HASH"
echo

# 4. 等消息进队列
sleep 6

# 5. 检查队列消息数增加
echo -e "${YELLOW}==> 检查队列消息数${NC}"
AFTER=$(queue_count)
echo "  上传后 messages = $AFTER"
DELTA=$((AFTER - BEFORE))
check_ge "队列消息数增加（delta=$DELTA）" "$DELTA" 1
echo

# 6. 校验消息内容（从队列 peek，不消费）
echo -e "${YELLOW}==> 校验消息内容${NC}"
# 用 management API 拉一条消息（ackmode=ack_requeue_true，相当于 peek）
# 拉一批消息（ack_requeue_true 相当于 peek，不消费）
MSG=$(curl -s -u "$RABBIT_AUTH" -X POST "$RABBIT/api/queues/%2f/$QUEUE/get" \
    -H 'Content-Type: application/json' \
    -d '{"count":100,"ackmode":"ack_requeue_true","encoding":"auto"}')

# 在返回的消息里找 hashcode 匹配的那条
FOUND=$(echo "$MSG" | UPLOAD_HASH="$UPLOAD_HASH" python3 -c "
import sys, json, os
target = os.environ['UPLOAD_HASH']
try:
    msgs = json.load(sys.stdin)
except Exception:
    print('')
    sys.exit(0)
for m in msgs:
    try:
        payload = json.loads(m['payload'])
        if payload.get('hashcode') == target:
            print(m['payload'])
            sys.exit(0)
    except Exception:
        continue
print('')
")

if [ -n "$FOUND" ]; then
    check "能在队列里找到刚上传的消息" "yes" "yes"
    MSG_NAME=$(echo "$FOUND" | field "['filename']")
    MSG_UID=$(echo "$FOUND" | field "['uid']")
    check "消息 filename 匹配" "$MSG_NAME" "$UPLOAD_NAME"
    check "消息含 uid 字段" "$([ -n "$MSG_UID" ] && echo yes || echo no)" "yes"
else
    check "能在队列里找到刚上传的消息" "no" "yes"
fi
echo

# 7. 清理临时文件
rm -f /tmp/mq_test_$$.txt

# 8. 汇总
echo -e "${YELLOW}==> 测试结果${NC}"
echo -e "  通过: ${GREEN}$pass${NC}"
echo -e "  失败: ${RED}$fail${NC}"

[ $fail -eq 0 ] && exit 0 || exit 1
