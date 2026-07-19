const $ = s => document.querySelector(s);
const $$ = s => document.querySelectorAll(s);
const term = $('#term');
const cmdInput = $('#cmd');
let sysInfo = {};
let cwd = '~';
let homeCwd = '~';

function esc(s) { return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); }
function ts() { return new Date().toLocaleTimeString('en-GB',{hour:'2-digit',minute:'2-digit',second:'2-digit'}); }
function shortCwd(p) {
    if (p === homeCwd) return '~';
    if (p.startsWith(homeCwd + '/')) return '~/' + p.slice(homeCwd.length + 1);
    const parts = p.split('/').filter(Boolean);
    return parts.length > 2 ? '.../' + parts.slice(-2).join('/') : p;
}
function promptStr() { return sysInfo.user ? sysInfo.user + '@' + sysInfo.host : '$'; }
function updateCwdUI() {
    const s = shortCwd(cwd);
    $('#f-cwd').textContent = s;
    $('#h-cwd').textContent = s;
    $('#prompt').textContent = promptStr() + ':' + s + '$';
    $('#term-title-cwd').textContent = s;
}
function setStatus(ok) {
    $('#s-dot').className = ok ? 'dot' : 'dot off';
    $('#s-label').textContent = ok ? 'online' : 'offline';
    $('#f-ws').textContent = ok ? 'api:ok' : 'api:--';
}
function meterColor(pct) { return pct > 85 ? 'crit' : pct > 60 ? 'warn' : 'ok'; }
function bar(pct, label) {
    const c = meterColor(pct);
    return `<div class="meter"><span class="meter-label">${esc(label)}</span><div class="meter-bar"><div class="meter-fill ${c}" style="width:${pct}%"></div></div><span class="meter-pct">${pct}%</span></div>`;
}

async function apiExec(cmd) {
    const r = await fetch('/api/exec', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({cmd})
    });
    return r.json();
}

/* ---- routing (history API) ---- */
function getViewFromPath() {
    const p = window.location.pathname;
    if (p.startsWith('/files')) return 'files';
    if (p.startsWith('/assistant')) return 'assistant';
    if (p.startsWith('/settings')) return 'settings';
    const name = p.replace(/^\/+/, '').replace(/\/+$/, '');
    return name || 'terminal';
}
function getFilesPathFromURL() {
    const p = window.location.pathname;
    if (!p.startsWith('/files/')) return null;
    return decodeURIComponent(p.slice('/files/'.length)) || '/';
}
function getConvFromURL() {
    const m = window.location.pathname.match(/^\/assistant\/([a-f0-9-]+)$/);
    return m ? m[1] : null;
}

function switchView(name) {
    $$('.view').forEach(v => v.classList.remove('active'));
    $$('.nav-link').forEach(l => l.classList.remove('on'));
    const view = $(`#view-${name}`);
    const link = $(`.nav-link[data-view="${name}"]`);
    if (view) view.classList.add('active');
    if (link) link.classList.add('on');
    $('aside').classList.remove('open');

    if (name === 'files') {
        const urlPath = getFilesPathFromURL();
        if (urlPath) filesLoad(urlPath, false);
        else if (!filesLoaded) filesLoad(cwd, false);
    }
    if (name === 'system') sysLoad();
    if (name === 'processes') procRefresh();
    if (name === 'terminal') cmdInput.focus();
    if (name === 'assistant') {
        const uuid = getConvFromURL();
        if (uuid) chatLoad(uuid);
        else if (!chatUuid) chatClear();
        convListLoad();
    }
}

function navigate(path, push) {
    if (push !== false) history.pushState(null, '', path);
    switchView(getViewFromPath());
}

window.addEventListener('popstate', () => switchView(getViewFromPath()));

$$('.nav-link').forEach(a => {
    a.addEventListener('click', e => {
        e.preventDefault();
        navigate(a.getAttribute('href'));
    });
});

/* ---- terminal ---- */
function addLine(text, cls) {
    const div = document.createElement('div');
    div.className = 'entry ' + cls;
    if (cls === 'cmd' || cls === 'out') {
        div.innerHTML = '<span class="ts">' + ts() + '</span>' + esc(text);
    } else {
        div.textContent = text;
    }
    term.appendChild(div);
    term.scrollTop = term.scrollHeight;
}

async function termSend() {
    const msg = cmdInput.value.trim();
    if (!msg) return;
    addLine(promptStr() + ':' + shortCwd(cwd) + '$ ' + msg, 'cmd');
    cmdInput.value = '';
    try {
        const d = await apiExec(msg);
        if (d.cwd) cwd = d.cwd;
        if (d.output) addLine(d.output, 'out');
        updateCwdUI();
    } catch (e) {
        addLine('error: ' + e.message, 'err');
    }
}
cmdInput.addEventListener('keydown', e => { if (e.key === 'Enter') termSend(); });

/* ---- files ---- */
let filesLoaded = false;
let filesDir = '';

async function filesLoad(path, pushUrl) {
    const target = path || cwd;
    const d = await apiExec('ls -la "' + target + '"');
    if (d.cwd) cwd = d.cwd;
    filesDir = target;
    if (pushUrl !== false) history.pushState(null, '', '/files' + (filesDir === '/' ? '' : filesDir));
    $('#files-path').value = filesDir;
    $('#files-title-path').textContent = shortCwd(filesDir);
    updateCwdUI();
    renderFiles(d.output);
    filesLoaded = true;
}

function renderFiles(output) {
    const list = $('#files-list');
    list.innerHTML = '';
    if (!output || output === '(no output)') {
        list.innerHTML = '<div class="file-empty">empty directory</div>';
        return;
    }
    const lines = output.split('\n').filter(l => l.trim());
    for (const line of lines) {
        const parts = line.split(/\s+/);
        if (parts.length < 9) continue;
        const perms = parts[0];
        const name = parts.slice(8).join(' ');
        if (name === '.' || name === '..') continue;
        const isDir = perms.startsWith('d');
        const isLink = perms.startsWith('l');
        const item = document.createElement('div');
        item.className = 'file-item' + (isDir ? ' dir' : '');
        const icon = isDir ? '/' : isLink ? '@' : '-';
        item.innerHTML = `<span class="icon">${icon}</span><span class="name">${esc(name)}</span><span class="meta">${isDir ? 'dir' : isLink ? 'link' : perms.slice(1)}</span>`;
        if (isDir) {
            item.addEventListener('click', () => {
                const sep = filesDir.endsWith('/') ? '' : '/';
                filesLoad(filesDir + sep + name);
            });
        }
        list.appendChild(item);
    }
    if (list.children.length === 0) list.innerHTML = '<div class="file-empty">empty directory</div>';
}

function filesUp() {
    if (filesDir === '/') return;
    const parts = filesDir.split('/').filter(Boolean);
    parts.pop();
    filesLoad('/' + parts.join('/') || '/');
}
function filesRefresh() { filesLoad(filesDir); }
function filesNavInput() {
    const p = $('#files-path').value.trim();
    if (p) filesLoad(p);
}
$('#files-path').addEventListener('keydown', e => { if (e.key === 'Enter') filesNavInput(); });

/* ---- system dashboard ---- */
let sysInterval = null;

async function sysLoad() {
    if (sysInterval) clearInterval(sysInterval);
    await sysRefresh();
    sysInterval = setInterval(sysRefresh, 3000);
}

async function sysRefresh() {
    const dash = $('#sys-dash');

    const [cpuRaw, memRaw, diskRaw, uptimeRaw, loadRaw, procCountRaw, hostnameRaw] = await Promise.all([
        apiExec("top -bn1 | grep %Cpu | head -1"),
        apiExec("free -b | grep Mem:"),
        apiExec("df -B1 / | grep /"),
        apiExec("uptime -p 2>/dev/null || uptime"),
        apiExec("cat /proc/loadavg"),
        apiExec("ps -e --no-headers | wc -l"),
        apiExec("hostname")
    ]);

    const cpuOut = (cpuRaw.output || '').trim();
    let cpuPct = 0;
    if (cpuOut) {
        const idle = cpuOut.match(/(\d+\.?\d*)\s*id/);
        if (idle) cpuPct = Math.round(100 - parseFloat(idle[1]));
    }

    const memLine = (memRaw.output || '').trim();
    let memTotal = 1, memUsed = 0, memAvail = 0;
    if (memLine) {
        const parts = memLine.split(/\s+/);
        memTotal = parseInt(parts[1]) || 1;
        memUsed = parseInt(parts[2]) || 0;
        memAvail = parseInt(parts[6]) || 0;
    }
    const memPct = Math.round((memUsed / memTotal) * 100);

    const diskLine = (diskRaw.output || '').trim();
    let diskTotal = 1, diskUsed = 0;
    if (diskLine) {
        const parts = diskLine.split(/\s+/);
        diskTotal = parseInt(parts[1]) || 1;
        diskUsed = parseInt(parts[2]) || 0;
    }
    const diskPct = Math.round((diskUsed / diskTotal) * 100);
    const load = (loadRaw.output || '? ? ?').split(/\s+/);
    const procs = parseInt(procCountRaw.output) || 0;

    function fmtBytes(b) {
        if (b > 1073741824) return (b / 1073741824).toFixed(1) + ' GB';
        if (b > 1048576) return (b / 1048576).toFixed(0) + ' MB';
        return (b / 1024).toFixed(0) + ' KB';
    }

    dash.innerHTML = `
        <div class="sys-row">
            <div class="sys-panel">
                <div class="sys-panel head">[ cpu ]</div>
                <div class="sys-panel body">
                    ${bar(cpuPct, 'usage')}
                    <div class="stat-row"><span class="k">load avg</span><span class="v">${esc(load[0]||'?')} / ${esc(load[1]||'?')} / ${esc(load[2]||'?')}</span></div>
                </div>
            </div>
            <div class="sys-panel">
                <div class="sys-panel head">[ memory ]</div>
                <div class="sys-panel body">
                    ${bar(memPct, 'usage')}
                    <div class="stat-row"><span class="k">total</span><span class="v">${fmtBytes(memTotal)}</span></div>
                    <div class="stat-row"><span class="k">used</span><span class="v">${fmtBytes(memUsed)}</span></div>
                    <div class="stat-row"><span class="k">avail</span><span class="v">${fmtBytes(memAvail)}</span></div>
                </div>
            </div>
            <div class="sys-panel">
                <div class="sys-panel head">[ disk / ]</div>
                <div class="sys-panel body">
                    ${bar(diskPct, 'usage')}
                    <div class="stat-row"><span class="k">total</span><span class="v">${fmtBytes(diskTotal)}</span></div>
                    <div class="stat-row"><span class="k">used</span><span class="v">${fmtBytes(diskUsed)}</span></div>
                    <div class="stat-row"><span class="k">free</span><span class="v">${fmtBytes(diskTotal - diskUsed)}</span></div>
                </div>
            </div>
        </div>
        <div class="sys-row">
            <div class="sys-panel">
                <div class="sys-panel head">[ system ]</div>
                <div class="sys-panel body">
                    <div class="stat-row"><span class="k">hostname</span><span class="v">${esc(hostnameRaw.output || sysInfo.host || '?')}</span></div>
                    <div class="stat-row"><span class="k">user</span><span class="v">${esc(sysInfo.user || '?')}</span></div>
                    <div class="stat-row"><span class="k">uptime</span><span class="v">${esc(uptimeRaw.output || '?')}</span></div>
                    <div class="stat-row"><span class="k">processes</span><span class="v">${procs}</span></div>
                </div>
            </div>
            <div class="sys-panel">
                <div class="sys-panel head">[ network ]</div>
                <div class="sys-panel body">
                    <div class="stat-row"><span class="k">bind</span><span class="v">127.0.0.1:8080</span></div>
                    <div class="stat-row"><span class="k">protocol</span><span class="v">HTTP + WebSocket</span></div>
                    <div class="stat-row"><span class="k">status</span><span class="v" style="color:var(--green)">listening</span></div>
                </div>
            </div>
            <div class="sys-panel">
                <div class="sys-panel head">[ engine ]</div>
                <div class="sys-panel body">
                    <div class="stat-row"><span class="k">version</span><span class="v">0.1.0-mvp</span></div>
                    <div class="stat-row"><span class="k">backend</span><span class="v">C++17 / Crow</span></div>
                    <div class="stat-row"><span class="k">ai</span><span class="v" style="color:var(--amber)">pending</span></div>
                    <div class="stat-row"><span class="k">build</span><span class="v">-O3 -march=native</span></div>
                </div>
            </div>
        </div>
    `;
}

/* ---- processes ---- */
async function procRefresh() {
    const body = $('#proc-body');
    body.innerHTML = '<tr><td colspan="11" style="text-align:center;color:var(--text-3);padding:30px">loading...</td></tr>';
    const d = await apiExec('ps aux --sort=-%cpu 2>/dev/null | head -50');
    const lines = (d.output || '').split('\n').slice(1).filter(Boolean);
    body.innerHTML = '';
    for (const line of lines) {
        const p = line.split(/\s+/);
        if (p.length < 11) continue;
        const tr = document.createElement('tr');
        tr.innerHTML = `<td>${esc(p[1])}</td><td>${esc(p[2])}</td><td>${esc(p[3])}</td><td>${esc(p[4])}</td><td>${esc(p[5])}</td><td>${esc(p[6])}</td><td>${esc(p[7])}</td><td>${esc(p[8])}</td><td>${esc(p[9])}</td><td>${esc(p[0])}</td><td class="cmd-col">${esc(p.slice(10).join(' '))}</td>`;
        body.appendChild(tr);
    }
    $('#proc-count').textContent = lines.length;
}

/* ---- assistant chat ---- */
let chatHistory = [];
let chatWaiting = false;
let chatUuid = null;
let aiProvider = 'gemini';
let geminiKey = '';
let claudeKey = '';

function chatMsg(role, text) {
    const el = document.createElement('div');
    el.className = 'ai-msg ' + role;
    el.innerHTML = `<div class="role">${esc(role)}</div><div class="body">${esc(text)}</div>`;
    return el;
}

function chatAppend(role, text) {
    const msgs = $('#ai-msgs');
    const empty = $('#ai-empty');
    if (empty) empty.remove();
    const el = chatMsg(role, text);
    msgs.appendChild(el);
    msgs.scrollTop = msgs.scrollHeight;
    return el;
}

async function chatSend() {
    const input = $('#ai-input');
    const text = input.value.trim();
    if (!text || chatWaiting) return;
    input.value = '';

    chatAppend('user', text);
    chatHistory.push({role: 'user', content: text});

    chatWaiting = true;
    $('#ai-send').disabled = true;

    const typing = document.createElement('div');
    typing.className = 'ai-typing';
    typing.textContent = 'thinking...';
    $('#ai-msgs').appendChild(typing);
    $('#ai-msgs').scrollTop = $('#ai-msgs').scrollHeight;

    try {
        const r = await fetch('/api/chat', {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({history: chatHistory})
        });
        const d = await r.json();
        typing.remove();
        if (d.error) {
            chatAppend('assistant', '[error: ' + d.error + ']');
        } else {
            chatAppend('assistant', d.reply);
            chatHistory.push({role: 'model', content: d.reply});
            chatAutoSave();
        }
    } catch (e) {
        typing.remove();
        chatAppend('assistant', '[error: ' + e.message + ']');
    }

    chatWaiting = false;
    $('#ai-send').disabled = false;
    input.focus();
}

async function chatAutoSave() {
    if (chatHistory.length < 1) return;
    try {
        const r = await fetch('/api/chat/save', {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({uuid: chatUuid || '', history: chatHistory})
        });
        const d = await r.json();
        if (d.uuid) {
            if (!chatUuid) {
                chatUuid = d.uuid;
                history.replaceState(null, '', '/assistant/' + chatUuid);
            }
            convListLoad();
        }
    } catch {}
}

function chatClear() {
    chatHistory = [];
    chatUuid = null;
    const msgs = $('#ai-msgs');
    msgs.innerHTML = '<div class="ai-empty" id="ai-empty"><div class="icon">[ ]</div><span>QuaSYS AI</span><span class="sub">Gemini powered assistant</span></div>';
    history.pushState(null, '', '/assistant');
    convListLoad();
}

async function chatLoad(uuid) {
    try {
        const r = await fetch('/api/chat/load/' + uuid);
        const d = await r.json();
        if (d.error) return;
        chatUuid = uuid;
        chatHistory = d.history || [];
        const msgs = $('#ai-msgs');
        msgs.innerHTML = '';
        for (const m of chatHistory) chatAppend(m.role, m.content);
        history.pushState(null, '', '/assistant/' + uuid);
        convListHighlight();
    } catch {}
}

async function convListLoad() {
    try {
        const r = await fetch('/api/chat/list');
        const d = await r.json();
        const list = $('#conv-list');
        list.innerHTML = '<div class="nav-title">conversations</div>';
        if (!d.conversations || d.conversations.length === 0) {
            list.innerHTML += '<div class="conv-empty">no conversations yet</div>';
            return;
        }
        for (const c of d.conversations) {
            const el = document.createElement('div');
            el.className = 'conv-item';
            el.dataset.uuid = c.uuid;
            el.innerHTML = `<span class="conv-title">${esc(c.title)}</span><button class="conv-rename" title="rename">&#9998;</button><button class="conv-del" title="delete">x</button>`;
            el.addEventListener('click', e => {
                if (e.target.classList.contains('conv-del')) {
                    e.stopPropagation();
                    convDelete(c.uuid);
                    return;
                }
                if (e.target.classList.contains('conv-rename')) {
                    e.stopPropagation();
                    convRenameStart(el, c.uuid, c.title);
                    return;
                }
                chatLoad(c.uuid);
            });
            list.appendChild(el);
        }
        convListHighlight();
    } catch {}
}

function convListHighlight() {
    $$('.conv-item').forEach(el => {
        el.classList.toggle('on', el.dataset.uuid === chatUuid);
    });
}

async function convDelete(uuid) {
    try {
        await fetch('/api/chat/delete/' + uuid, {method: 'DELETE'});
        if (chatUuid === uuid) chatClear();
        convListLoad();
    } catch {}
}

function convRenameStart(el, uuid, oldTitle) {
    const titleEl = el.querySelector('.conv-title');
    const input = document.createElement('input');
    input.type = 'text';
    input.className = 'conv-rename-input';
    input.value = oldTitle;
    titleEl.replaceWith(input);
    input.focus();
    input.select();

    const finish = async (save) => {
        const newTitle = save ? input.value.trim() : oldTitle;
        if (save && newTitle && newTitle !== oldTitle) {
            try {
                await fetch('/api/chat/rename/' + uuid, {
                    method: 'PUT', headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({title: newTitle})
                });
            } catch {}
        }
        const span = document.createElement('span');
        span.className = 'conv-title';
        span.textContent = newTitle || oldTitle;
        input.replaceWith(span);
    };

    input.addEventListener('keydown', e => {
        if (e.key === 'Enter') { e.preventDefault(); finish(true); }
        if (e.key === 'Escape') finish(false);
    });
    input.addEventListener('blur', () => finish(true));
}

$('#ai-input').addEventListener('keydown', e => { if (e.key === 'Enter') chatSend(); });

/* ---- theme ---- */
function getTheme() { return localStorage.getItem('quasys-theme') || 'dark'; }
function toggleTheme() {
    const t = getTheme() === 'dark' ? 'light' : 'dark';
    localStorage.setItem('quasys-theme', t);
    applyTheme(t);
}
function applyTheme(t) {
    if (t === 'light') {
        document.documentElement.style.setProperty('--bg', '#f1f5f9');
        document.documentElement.style.setProperty('--surface', '#ffffff');
        document.documentElement.style.setProperty('--surface-2', '#f8fafc');
        document.documentElement.style.setProperty('--surface-3', '#e2e8f0');
        document.documentElement.style.setProperty('--border', '#cbd5e1');
        document.documentElement.style.setProperty('--border-bright', '#94a3b8');
        document.documentElement.style.setProperty('--text', '#1e293b');
        document.documentElement.style.setProperty('--text-2', '#475569');
        document.documentElement.style.setProperty('--text-3', '#94a3b8');
        document.documentElement.style.setProperty('--accent-dim', '#dbeafe');
    } else {
        document.documentElement.style.setProperty('--bg', '#0f172a');
        document.documentElement.style.setProperty('--surface', '#1e293b');
        document.documentElement.style.setProperty('--surface-2', '#253449');
        document.documentElement.style.setProperty('--surface-3', '#334155');
        document.documentElement.style.setProperty('--border', '#334155');
        document.documentElement.style.setProperty('--border-bright', '#475569');
        document.documentElement.style.setProperty('--text', '#e2e8f0');
        document.documentElement.style.setProperty('--text-2', '#94a3b8');
        document.documentElement.style.setProperty('--text-3', '#64748b');
        document.documentElement.style.setProperty('--accent-dim', '#1e3a5f');
    }
}
applyTheme(getTheme());

/* ---- sidebar toggle ---- */
function toggleSidebar() {
    const aside = document.querySelector('aside');
    aside.classList.toggle('collapsed');
    localStorage.setItem('quasys-sidebar', aside.classList.contains('collapsed') ? 'collapsed' : 'expanded');
}
(function() {
    if (localStorage.getItem('quasys-sidebar') === 'collapsed')
        document.querySelector('aside').classList.add('collapsed');
})();

function aiProviderChange() {
    const sel = $('#ai-provider');
    aiProvider = sel.value;
    // save to settings
    fetch('/api/settings', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({provider: aiProvider, gemini_key: geminiKey, claude_key: claudeKey})
    });
}

/* ---- settings ---- */
function toggleKeyVis(inputId, btn) {
    const input = $('#' + inputId);
    if (input.type === 'password') {
        input.type = 'text';
        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg>';
    } else {
        input.type = 'password';
        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>';
    }
}
async function settingsLoad() {
    try {
        const r = await fetch('/api/settings');
        const d = await r.json();
        aiProvider = d.provider || 'gemini';
        geminiKey = d.gemini_key || '';
        claudeKey = d.claude_key || '';
        const provEl = $('#set-provider');
        const gemEl = $('#set-gemini-key');
        const clEl = $('#set-claude-key');
        if (provEl) provEl.value = aiProvider;
        if (gemEl) gemEl.value = geminiKey;
        if (clEl) clEl.value = claudeKey;
        const aiProv = $('#ai-provider');
        if (aiProv) aiProv.value = aiProvider;
    } catch {}
}
async function settingsSave() {
    const provider = $('#set-provider').value;
    const gemini_key = $('#set-gemini-key').value.trim();
    const claude_key = $('#set-claude-key').value.trim();
    const status = $('#set-status');
    try {
        await fetch('/api/settings', {
            method: 'POST', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({provider, gemini_key, claude_key})
        });
        aiProvider = provider;
        geminiKey = gemini_key;
        claudeKey = claude_key;
        const aiProv = $('#ai-provider');
        if (aiProv) aiProv.value = aiProvider;
        status.textContent = 'saved';
        status.className = 'settings-status ok';
    } catch (e) {
        status.textContent = 'error';
        status.className = 'settings-status err';
    }
    setTimeout(() => { status.textContent = ''; status.className = 'settings-status'; }, 2000);
}

/* ---- init ---- */
async function init() {
    try {
        const res = await fetch('/api/info');
        const d = await res.json();
        sysInfo = d;
        $('#aside-foot').textContent = d.user + '@' + d.host;
        $('#h-user').textContent = d.user + '@' + d.host;
        if (d.home) homeCwd = d.home;
        setStatus(true);
        term.innerHTML = '';
        addLine('connected.', 'sys');
        const r = await apiExec('pwd');
        if (r.output) cwd = r.output.trim();
        updateCwdUI();
    } catch {
        setStatus(false);
        addLine('failed to connect to backend.', 'err');
    }
    switchView(getViewFromPath());
    settingsLoad();
}
init();
