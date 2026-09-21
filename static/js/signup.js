document.getElementById('signupForm').addEventListener('submit', async (e) => {
    e.preventDefault();

    const username = document.getElementById('username').value.trim();
    const password = document.getElementById('password').value;
    const msg = document.getElementById('msg');

    if (!username || !password) {
        msg.textContent = '用户名和密码不能为空';
        return;
    }

    // 后端接受 x-www-form-urlencoded
    const body = new URLSearchParams();
    body.append('username', username);
    body.append('password', password);

    try {
        const resp = await fetch('/user/signup', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await resp.json();

        if (data.code === 0) {
            msg.textContent = '注册成功，正在跳转到登录页...';
            setTimeout(() => {
                window.location.href = '/static/login.html';
            }, 800);
        } else {
            msg.textContent = '注册失败：' + (data.msg || '未知错误');
        }
    } catch (err) {
        msg.textContent = '请求出错：' + err.message;
    }
});
