import http from "node:http";
import { randomUUID } from "node:crypto";

const port = Number(process.env.PORT || 8080);
const adminUser = process.env.ADMIN_USER || "admin";
const adminPass = process.env.ADMIN_PASS || "69696966";
const expectedDeviceId = process.env.DEVICE_ID || "bell-robot-1";
const expectedDeviceToken = process.env.DEVICE_TOKEN || "change-this-device-token";
const offlineAfterMs = Number(process.env.OFFLINE_AFTER_MS || 5000);

const deviceState = {
  lastSeenMs: 0,
  status: null,
  lastResult: null,
};
const commandQueue = [];
const captureWaiters = new Map();

function send(res, status, body, headers = {}) {
  const payload = typeof body === "string" || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Cache-Control": "no-store",
    ...headers,
  });
  res.end(payload);
}

function sendJson(res, status, body) {
  send(res, status, body, { "Content-Type": "application/json; charset=utf-8" });
}

function htmlEscape(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll("\"", "&quot;");
}

function unauthorized(res) {
  send(res, 401, "authentication required", {
    "WWW-Authenticate": "Basic realm=\"Bell Robot\"",
    "Content-Type": "text/plain; charset=utf-8",
  });
}

function checkAdmin(req, res) {
  const auth = req.headers.authorization || "";
  if (!auth.startsWith("Basic ")) {
    unauthorized(res);
    return false;
  }
  const decoded = Buffer.from(auth.slice(6), "base64").toString("utf8");
  const separator = decoded.indexOf(":");
  const user = separator >= 0 ? decoded.slice(0, separator) : "";
  const pass = separator >= 0 ? decoded.slice(separator + 1) : "";
  if (user !== adminUser || pass !== adminPass) {
    unauthorized(res);
    return false;
  }
  return true;
}

function checkDevice(req, res) {
  const auth = req.headers.authorization || "";
  if (auth !== `Bearer ${expectedDeviceToken}`) {
    send(res, 401, "invalid device token", { "Content-Type": "text/plain; charset=utf-8" });
    return false;
  }
  return true;
}

async function readBody(req, maxBytes = 1024 * 1024) {
  const chunks = [];
  let total = 0;
  for await (const chunk of req) {
    total += chunk.length;
    if (total > maxBytes) {
      throw new Error("request too large");
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

async function readJson(req, maxBytes) {
  const body = await readBody(req, maxBytes);
  if (body.length === 0) {
    return {};
  }
  return JSON.parse(body.toString("utf8"));
}

function isOnline() {
  return deviceState.lastSeenMs > 0 && Date.now() - deviceState.lastSeenMs <= offlineAfterMs;
}

function enqueueCommand(type, payload = {}) {
  const command = {
    id: randomUUID(),
    type,
    payload,
    created_at: Date.now(),
  };
  commandQueue.push(command);
  return command;
}

function publicStatus() {
  return {
    online: isOnline(),
    last_seen_ms: deviceState.lastSeenMs,
    status: deviceState.status,
    queued_commands: commandQueue.length,
    last_result: deviceState.lastResult,
  };
}

function sendIndex(res) {
  const title = "Bell Robot";
  send(res, 200, `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>${title}</title>
  <style>
    body{margin:0;background:#101214;color:#eef2f5;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
    main{max-width:760px;margin:0 auto;padding:18px 14px 28px}
    h1{font-size:24px;margin:4px 0 16px}
    section{border-top:1px solid #2b3137;padding:14px 0}
    img{width:100%;height:auto;background:#222;display:block}
    button,input{font-size:17px}
    button{padding:10px 14px;margin:6px 6px 6px 0;background:#2d6cdf;color:white;border:0;border-radius:6px}
    input{box-sizing:border-box;width:100%;padding:10px;background:#181c20;color:#eef2f5;border:1px solid #424a53;border-radius:6px}
    label{display:block;margin:10px 0 5px;color:#b8c2cc}
    code{color:#9fd0ff}
    .row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
    .muted{color:#9aa6b2}
    #msg{min-height:24px;color:#9fdb9f}
    pre{white-space:pre-wrap;background:#181c20;padding:10px;border-radius:6px;overflow:auto}
  </style>
</head>
<body>
  <main>
    <h1>${htmlEscape(title)}</h1>
    <section>
      <img id="frame" alt="camera snapshot">
      <p><button onclick="refreshFrame()">刷新画面</button><span id="captureMsg" class="muted"></span></p>
    </section>
    <section>
      <div id="summary" class="muted">加载中...</div>
      <pre id="status"></pre>
    </section>
    <section>
      <form onsubmit="saveSettings(event)">
        <div class="row">
          <div><label for="sit">倒计时（分钟）</label><input id="sit" type="number" min="1" max="180" step="1" required></div>
          <div><label for="away">离场容忍（分钟）</label><input id="away" type="number" min="1" max="5" step="1" required></div>
        </div>
        <button type="submit">保存设置</button>
        <button type="button" onclick="resetTimer()">重置</button>
        <span id="msg"></span>
      </form>
    </section>
  </main>
  <script>
    async function getJson(url, options){
      const r = await fetch(url, options);
      if(!r.ok) throw new Error(await r.text());
      return await r.json();
    }
    async function loadStatus(){
      try{
        const data = await getJson('/api/status');
        const st = data.status || {};
        document.getElementById('summary').textContent = data.online ? '设备在线' : '设备离线';
        document.getElementById('status').textContent = JSON.stringify(st, null, 2);
        if(st.sit_minutes) document.getElementById('sit').value = st.sit_minutes;
        if(st.away_minutes) document.getElementById('away').value = st.away_minutes;
      }catch(e){
        document.getElementById('summary').textContent = '状态读取失败';
      }
    }
    async function refreshFrame(){
      const msg = document.getElementById('captureMsg');
      msg.textContent = '请求中...';
      try{
        const r = await fetch('/api/capture.jpg?ts=' + Date.now());
        if(!r.ok) throw new Error(await r.text());
        const blob = await r.blob();
        document.getElementById('frame').src = URL.createObjectURL(blob);
        msg.textContent = '';
      }catch(e){
        msg.textContent = '画面不可用';
      }
    }
    async function saveSettings(e){
      e.preventDefault();
      const msg = document.getElementById('msg');
      msg.textContent = 'Saving...';
      try{
        await getJson('/api/settings', {
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body:JSON.stringify({
            sit_minutes:Number(document.getElementById('sit').value),
            away_minutes:Number(document.getElementById('away').value)
          })
        });
        msg.textContent = 'Saved';
      }catch(e){
        msg.textContent = 'Save failed';
      }
    }
    async function resetTimer(){
      const msg = document.getElementById('msg');
      msg.textContent = 'Resetting...';
      try{
        await getJson('/api/reset', { method:'POST' });
        msg.textContent = 'Reset queued';
      }catch(e){
        msg.textContent = 'Reset failed';
      }
    }
    loadStatus();
    setInterval(loadStatus, 1000);
  </script>
</body>
</html>`, { "Content-Type": "text/html; charset=utf-8" });
}

async function handleAdmin(req, res, url) {
  if (!checkAdmin(req, res)) {
    return;
  }
  if (req.method === "GET" && url.pathname === "/") {
    sendIndex(res);
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/status") {
    sendJson(res, 200, publicStatus());
    return;
  }
  if (req.method === "POST" && url.pathname === "/api/settings") {
    const body = await readJson(req, 4096);
    const sit = Number(body.sit_minutes);
    const away = Number(body.away_minutes);
    if (!Number.isInteger(sit) || sit < 1 || sit > 180 ||
        !Number.isInteger(away) || away < 1 || away > 5) {
      sendJson(res, 400, { error: "settings out of range" });
      return;
    }
    const command = enqueueCommand("set_settings", { sit_minutes: sit, away_minutes: away });
    sendJson(res, 202, { queued: true, command_id: command.id });
    return;
  }
  if (req.method === "POST" && url.pathname === "/api/reset") {
    const command = enqueueCommand("reset");
    sendJson(res, 202, { queued: true, command_id: command.id });
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/capture.jpg") {
    if (!isOnline()) {
      send(res, 503, "device offline", { "Content-Type": "text/plain; charset=utf-8" });
      return;
    }
    const command = enqueueCommand("capture");
    const waitResult = await new Promise((resolve) => {
      const timer = setTimeout(() => {
        captureWaiters.delete(command.id);
        resolve(null);
      }, 10000);
      captureWaiters.set(command.id, { resolve, timer });
    });
    if (!waitResult) {
      send(res, 504, "capture timeout", { "Content-Type": "text/plain; charset=utf-8" });
      return;
    }
    send(res, 200, waitResult.image, {
      "Content-Type": "image/jpeg",
      "Content-Length": String(waitResult.image.length),
    });
    return;
  }
  sendJson(res, 404, { error: "not found" });
}

async function handleDevice(req, res, url) {
  if (!checkDevice(req, res)) {
    return;
  }
  if (req.method === "POST" && url.pathname === "/device/poll") {
    const body = await readJson(req, 16384);
    if (body.device_id !== expectedDeviceId) {
      sendJson(res, 403, { error: "device id mismatch" });
      return;
    }
    deviceState.lastSeenMs = Date.now();
    deviceState.status = body.status || null;
    const command = commandQueue.shift() || null;
    sendJson(res, 200, { command });
    return;
  }
  if (req.method === "POST" && url.pathname === "/device/result") {
    const body = await readJson(req, 4096);
    if (body.device_id !== expectedDeviceId) {
      sendJson(res, 403, { error: "device id mismatch" });
      return;
    }
    deviceState.lastResult = {
      command_id: body.command_id || "",
      ok: body.ok === true,
      message: body.message || "",
      ts: Date.now(),
    };
    sendJson(res, 200, { ok: true });
    return;
  }
  if (req.method === "POST" && url.pathname === "/device/capture") {
    const commandId = url.searchParams.get("command_id") || "";
    const waiter = captureWaiters.get(commandId);
    if (!waiter) {
      sendJson(res, 404, { error: "capture command not waiting" });
      return;
    }
    const image = await readBody(req, 512 * 1024);
    clearTimeout(waiter.timer);
    captureWaiters.delete(commandId);
    waiter.resolve({ image });
    sendJson(res, 200, { ok: true });
    return;
  }
  sendJson(res, 404, { error: "not found" });
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);
    if (url.pathname.startsWith("/device/")) {
      await handleDevice(req, res, url);
    } else {
      await handleAdmin(req, res, url);
    }
  } catch (error) {
    sendJson(res, 500, { error: error instanceof Error ? error.message : "server error" });
  }
});

server.listen(port, () => {
  console.log(`Bell Robot relay listening on ${port}`);
});
