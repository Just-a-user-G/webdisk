document.getElementById('loginForm').addEventListener('submit', async (e) => {
    e.preventDefault();

    const username = document.getElementById('username').value.trim();
    const password = document.getElementById('password').value;
    const msg = document.getElementById('msg');

    if (!username || !password) {
        msg.textContent = '用户名和密码不能为空';
        return;
    }

    const body = new URLSearchParams();
    body.append('username', username);
    body.append('password', password);

    try {
        const resp = await fetch('/user/signin', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await resp.json();

        if (data.code === 0) {
            // 把 username 和 token 存到 localStorage
            localStorage.setItem('username', data.data.Username);
            localStorage.setItem('token', data.data.Token);

            msg.textContent = '登录成功，正在跳转...';
            setTimeout(() => {
                window.location.href = '/static/home.html';
            }, 800);
        } else {
            msg.textContent = '登录失败：' + (data.msg || '未知错误');
        }
    } catch (err) {
        msg.textContent = '请求出错：' + err.message;
    }
});
