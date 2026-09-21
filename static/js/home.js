// 字节数转可读格式
function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

const username = localStorage.getItem('username');
const token = localStorage.getItem('token');
const PAGE_SIZE = 10;
let currentPage = 1;
let currentKeyword = '';

// 没登录直接踢回登录页
if (!username || !token) {
    window.location.href = '/static/login.html';
}

// 退出登录
document.getElementById('logoutLink').addEventListener('click', (e) => {
    e.preventDefault();
    localStorage.removeItem('username');
    localStorage.removeItem('token');
    window.location.href = '/static/login.html';
});

// 加载用户信息
async function loadUserInfo() {
    const url = `/user/info?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
    const resp = await fetch(url);
    const data = await resp.json();

    const div = document.getElementById('userInfo');
    if (data.code === 0) {
        div.textContent = `你好，${data.data.Username}（注册于 ${data.data.SignupAt}）`;
    } else {
        div.textContent = '获取用户信息失败：' + data.msg;
    }
}

// 加载文件列表
async function loadFiles() {
    const offset = (currentPage - 1) * PAGE_SIZE;
    const url = `/file/query?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
    const body = new URLSearchParams();
    body.append('limit', PAGE_SIZE);
    body.append('offset', offset);
    if (currentKeyword) body.append('keyword', currentKeyword);

    const resp = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body.toString()
    });
    const data = await resp.json();

    const tbody = document.querySelector('#fileTable tbody');
    tbody.innerHTML = '';

    if (data.code !== 0) {
        tbody.innerHTML = `<tr><td colspan="4">加载失败：${data.msg}</td></tr>`;
        return;
    }

    for (const f of data.data) {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${f.FileName}</td>
            <td>${formatSize(f.FileSize)}</td>
            <td>${f.UploadAt}</td>
            <td>
                <a href="#" data-hash="${f.FileHash}" data-name="${f.FileName}" class="downloadLink">下载</a>
                &nbsp;|&nbsp;
                <a href="#" data-hash="${f.FileHash}" data-name="${f.FileName}" class="renameLink">重命名</a>
                &nbsp;|&nbsp;
                <a href="#" data-hash="${f.FileHash}" data-name="${f.FileName}" class="deleteLink">删除</a>
            </td>
        `;
        tbody.appendChild(tr);
    }

    // 更新翻页控件
    document.getElementById('pageInfo').textContent = `第 ${currentPage} 页`;
    document.getElementById('prevBtn').disabled = (currentPage === 1);
    document.getElementById('nextBtn').disabled = (data.data.length < PAGE_SIZE);

    // 绑定下载
    document.querySelectorAll('.downloadLink').forEach(a => {
        a.addEventListener('click', (e) => {
            e.preventDefault();
            const hash = a.getAttribute('data-hash');
            const name = a.getAttribute('data-name');
            const url = `/file/download?username=${encodeURIComponent(username)}` +
                        `&token=${encodeURIComponent(token)}` +
                        `&filename=${encodeURIComponent(name)}` +
                        `&filehash=${encodeURIComponent(hash)}`;
            window.location.href = url;
        });
    });

    // 绑定删除
    document.querySelectorAll('.deleteLink').forEach(a => {
        a.addEventListener('click', async (e) => {
            e.preventDefault();
            const hash = a.getAttribute('data-hash');
            const name = a.getAttribute('data-name');

            if (!confirm(`确定删除「${name}」吗？`)) return;

            const url = `/file/delete?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
            const body = new URLSearchParams();
            body.append('filename', name);
            body.append('filehash', hash);

            try {
                const resp = await fetch(url, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: body.toString()
                });
                const data = await resp.json();
                if (data.code === 0) {
                    loadFiles();
                } else {
                    alert('删除失败：' + data.msg);
                }
            } catch (err) {
                alert('请求出错：' + err.message);
            }
        });
    });

    // 绑定重命名
    document.querySelectorAll('.renameLink').forEach(a => {
        a.addEventListener('click', async (e) => {
            e.preventDefault();
            const hash = a.getAttribute('data-hash');
            const oldName = a.getAttribute('data-name');

            const newName = prompt('输入新文件名：', oldName);
            if (!newName || newName === oldName) return;
            if (newName.length > 255) {
                alert('文件名太长（最多 255 字符）');
                return;
            }

            const url = `/file/rename?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
            const body = new URLSearchParams();
            body.append('old_filename', oldName);
            body.append('new_filename', newName);
            body.append('filehash', hash);

            try {
                const resp = await fetch(url, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: body.toString()
                });
                const data = await resp.json();
                if (data.code === 0) {
                    loadFiles();
                } else {
                    alert('重命名失败：' + data.msg);
                }
            } catch (err) {
                alert('请求出错：' + err.message);
            }
        });
    });
}

// 上传
// 分片阈值：超过 5MB 走分片上传
const CHUNK_THRESHOLD = 5 * 1024 * 1024;
// 每片大小：1MB
const CHUNK_SIZE = 1 * 1024 * 1024;

document.getElementById('uploadBtn').addEventListener('click', async () => {
    const input = document.getElementById('fileInput');
    const msg = document.getElementById('uploadMsg');
    const progressWrap = document.getElementById('progressWrap');
    const progressBar = document.getElementById('progressBar');
    const progressText = document.getElementById('progressText');

    if (!input.files.length) {
        msg.textContent = '请先选择文件';
        msg.className = 'msg';
        return;
    }

    const file = input.files[0];
    msg.textContent = '';
    msg.className = 'msg';

    try {
        if (file.size <= CHUNK_THRESHOLD) {
            // 小文件：单次上传
            await uploadSmall(file, msg);
        } else {
            // 大文件：分片上传
            progressWrap.style.display = 'block';
            await uploadChunked(file, msg, progressBar, progressText);
            progressWrap.style.display = 'none';
        }

        msg.textContent = '上传成功';
        msg.className = 'msg success';
        input.value = '';
        loadFiles();
    } catch (err) {
        msg.textContent = '上传失败：' + err.message;
        msg.className = 'msg';
        progressWrap.style.display = 'none';
    }
});

// 小文件单次上传
async function uploadSmall(file, msg) {
    const fd = new FormData();
    fd.append('file', file);

    const url = `/file/upload?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
    const resp = await fetch(url, { method: 'POST', body: fd });
    const data = await resp.json();
    if (data.code !== 0) throw new Error(data.msg || '上传失败');
}

// 生成 localStorage 的 key，用于断点续传
function resumeKey(file) {
    return `resume_${file.name}_${file.size}`;
}

// 查已收到的分片，返回数组；失败返回空数组
async function fetchReceivedChunks(uploadId) {
    const url = `/file/upload/status?username=${encodeURIComponent(username)}` +
                `&token=${encodeURIComponent(token)}` +
                `&upload_id=${encodeURIComponent(uploadId)}`;
    try {
        const resp = await fetch(url);
        const data = await resp.json();
        if (data.code === 0) return data.data.ReceivedChunks || [];
    } catch (e) {
        // 忽略，视为没有已传分片
    }
    return [];
}

// 大文件分片上传（支持断点续传）
async function uploadChunked(file, msg, progressBar, progressText) {
    const totalChunks = Math.ceil(file.size / CHUNK_SIZE);
    const totalSize = file.size;
    const key = resumeKey(file);

    let uploadId = localStorage.getItem(key);
    let received = [];

    // 1. 如果有历史 upload_id，尝试续传
    if (uploadId) {
        received = await fetchReceivedChunks(uploadId);
        if (received.length === 0 && received !== null) {
            // 会话可能已失效，但 ReceivedChunks 为空也可能是真的没传过
            // 保守起见，继续用这个 upload_id（如果失效，chunk 请求会返回错误）
        }
        msg.textContent = `发现未完成上传，已传 ${received.length}/${totalChunks} 片，继续上传...`;
    } else {
        // 2. 没有历史，走 init
        const initBody = new URLSearchParams();
        initBody.append('filename', file.name);
        initBody.append('total_chunks', totalChunks);
        initBody.append('total_size', totalSize);

        const initUrl = `/file/upload/init?username=${encodeURIComponent(username)}&token=${encodeURIComponent(token)}`;
        const initResp = await fetch(initUrl, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: initBody.toString()
        });
        const initData = await initResp.json();
        if (initData.code !== 0) throw new Error('init 失败：' + initData.msg);

        uploadId = initData.data.UploadID;
        localStorage.setItem(key, uploadId);
        received = [];
    }

    // 3. 逐片上传，跳过已传的
    const receivedSet = new Set(received);
    let doneCount = received.length;

    // 进度初始化
    progressBar.value = Math.round(doneCount / totalChunks * 100);
    progressText.textContent = `上传中 ${progressBar.value}% (${doneCount}/${totalChunks})`;

    for (let i = 0; i < totalChunks; i++) {
        if (receivedSet.has(i)) continue;

        const start = i * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, file.size);
        const blob = file.slice(start, end);

        const fd = new FormData();
        fd.append('chunk', blob, `chunk${i}`);

        const chunkUrl = `/file/upload/chunk?username=${encodeURIComponent(username)}` +
                         `&token=${encodeURIComponent(token)}` +
                         `&upload_id=${encodeURIComponent(uploadId)}` +
                         `&chunk_index=${i}`;
        const chunkResp = await fetch(chunkUrl, { method: 'POST', body: fd });
        const chunkData = await chunkResp.json();

        if (chunkData.code !== 0) {
            // 会话失效或其它错误：清掉本地记录，让下次重新 init
            if (chunkData.msg && chunkData.msg.includes('not found')) {
                localStorage.removeItem(key);
            }
            throw new Error(`分片 ${i} 上传失败：${chunkData.msg}`);
        }

        doneCount++;
        const percent = Math.round(doneCount / totalChunks * 100);
        progressBar.value = percent;
        progressText.textContent = `上传中 ${percent}% (${doneCount}/${totalChunks})`;
        msg.textContent = `上传中 ${percent}%`;
    }

    // 4. complete
    const completeUrl = `/file/upload/complete?username=${encodeURIComponent(username)}` +
                        `&token=${encodeURIComponent(token)}` +
                        `&upload_id=${encodeURIComponent(uploadId)}`;
    const completeResp = await fetch(completeUrl, { method: 'POST' });
    const completeData = await completeResp.json();
    if (completeData.code !== 0) throw new Error('合并失败：' + completeData.msg);

    // 5. 成功后清掉本地续传记录
    localStorage.removeItem(key);

    progressBar.value = 100;
    progressText.textContent = '上传完成';
}

document.getElementById('refreshBtn').addEventListener('click', loadFiles);

document.getElementById('searchBtn').addEventListener('click', () => {
    currentKeyword = document.getElementById('searchInput').value.trim();
    currentPage = 1;
    loadFiles();
});

document.getElementById('clearBtn').addEventListener('click', () => {
    document.getElementById('searchInput').value = '';
    currentKeyword = '';
    currentPage = 1;
    loadFiles();
});

// 回车也触发搜索
document.getElementById('searchInput').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        e.preventDefault();
        document.getElementById('searchBtn').click();
    }
});

document.getElementById('prevBtn').addEventListener('click', () => {
    if (currentPage > 1) {
        currentPage--;
        loadFiles();
    }
});

document.getElementById('nextBtn').addEventListener('click', () => {
    currentPage++;
    loadFiles();
});

// 初始化
loadUserInfo();
loadFiles();
