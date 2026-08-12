let ttyLogSocket;
let ttyLogReconnectTimer;
let ttyLogRaw = '';

function setTtyLogStatus(text, statusClass) {
  const status = document.getElementById('ttyLogStatus');
  status.lastChild.textContent = ` ${text}`;
  status.className = `stream-status ${statusClass}`;
}

function ttyToken() {
  return document.getElementById('ttyToken').value;
}

function ttyTokenQuery() {
  return `token=${encodeURIComponent(ttyToken())}`;
}

function loadSavedTtyToken() {
  document.getElementById('ttyToken').value = localStorage.getItem('ttyToken') || '';
}

function saveTtyToken() {
  localStorage.setItem('ttyToken', ttyToken());
}

function ttyLogLevelEnabled(level) {
  const id = {
    INFO: 'ttyLogInfo',
    WARN: 'ttyLogWarn',
    ERROR: 'ttyLogError',
  }[level] || 'ttyLogInfo';
  return document.getElementById(id).checked;
}

function selectedTtyLogLevels() {
  return ['INFO', 'WARN', 'ERROR'].filter((level) => ttyLogLevelEnabled(level)).join(', ') || 'none';
}

function syncTtyLogLevelSummary() {
  const summary = document.getElementById('obsTtyLogLevels');
  if (summary) {
    summary.textContent = selectedTtyLogLevels();
  }
}

function ttyLogLineLevel(line) {
  const match = line.match(/^\[(INFO|WARN|ERROR)\]\s/);
  return match ? match[1] : 'INFO';
}

function escapeHtml(text) {
  return text
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function renderTtyLog(log) {
  ttyLogRaw = log || '';
  syncTtyLogLevelSummary();
  const html = ttyLogRaw
    .split('\n')
    .filter((line) => line.length > 0)
    .filter((line) => ttyLogLevelEnabled(ttyLogLineLevel(line)))
    .map((line) => {
      const level = ttyLogLineLevel(line).toLowerCase();
      return `<span class="tty-log-line tty-log-${level}">${escapeHtml(line)}</span>`;
    })
    .join('');
  document.getElementById('ttyLog').innerHTML = html;
}

async function execTty() {
  if (!document.getElementById('ttyCmd').value.trim()) {
    assertFieldValid('ttyCmd', 'TTY command is required');
  }
  assertFieldValid('ttyCmd', '');
  saveTtyToken();
  const response = await postFormJson('api/tty/exec', {
    cmd: document.getElementById('ttyCmd').value,
    token: ttyToken(),
  });

  document.getElementById('ttyOut').textContent = response.data?.output || '';
  await refresh();
  showToast('Command sent');
}

async function loadTtyLog() {
  setTtyLogStatus('polling fallback', 'stream-status-fallback');
  const response = await getJson(`api/tty/log?${ttyTokenQuery()}`);
  renderTtyLog(response.data?.log || '');
}

function connectTtyLogSocket() {
  saveTtyToken();
  clearTimeout(ttyLogReconnectTimer);
  if (ttyLogSocket) {
    const oldSocket = ttyLogSocket;
    ttyLogSocket = null;
    oldSocket.close();
  }

  if (!window.WebSocket || !window.location.hostname) {
    setTtyLogStatus('polling fallback', 'stream-status-fallback');
    loadTtyLog().catch(() => {});
    return;
  }

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  setTtyLogStatus('connecting', 'stream-status-connecting');
  const socket = new WebSocket(`${protocol}//${window.location.hostname}:81/?${ttyTokenQuery()}`);
  ttyLogSocket = socket;

  socket.addEventListener('message', (event) => {
    renderTtyLog(event.data);
  });

  socket.addEventListener('open', () => {
    setTtyLogStatus('connected', 'stream-status-connected');
    showToast('TTY log stream connected');
  });

  socket.addEventListener('close', () => {
    if (ttyLogSocket === socket) {
      setTtyLogStatus('reconnecting', 'stream-status-connecting');
      ttyLogReconnectTimer = setTimeout(connectTtyLogSocket, 2500);
    }
  });

  socket.addEventListener('error', () => {
    setTtyLogStatus('connection error', 'stream-status-error');
    socket.close();
  });
}
