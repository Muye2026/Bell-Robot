import http from "node:http";
import { randomUUID } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";

const port = Number(process.env.PORT || 8080);
const offlineAfterMs = Number(process.env.OFFLINE_AFTER_MS || 5000);
const dataDir = process.env.DATA_DIR || "/data";
const registryPath = path.join(dataDir, "devices.json");

const devices = new Map();

function createDeviceRecord(id, token) {
  return {
    id,
    token,
    firstSeenMs: Date.now(),
    lastSeenMs: 0,
    status: null,
    lastResult: null,
    commandQueue: [],
    captureWaiters: new Map(),
  };
}

async function loadRegistry() {
  try {
    const raw = await readFile(registryPath, "utf8");
    const parsed = JSON.parse(raw);
    for (const item of parsed.devices || []) {
      if (typeof item.id !== "string" || typeof item.token !== "string") {
        continue;
      }
      const record = createDeviceRecord(item.id, item.token);
      record.firstSeenMs = Number(item.firstSeenMs || Date.now());
      devices.set(record.id, record);
    }
  } catch (error) {
    if (error?.code !== "ENOENT") {
      console.warn("failed to load device registry:", error.message);
    }
  }
}

async function saveRegistry() {
  await mkdir(dataDir, { recursive: true });
  const body = {
    devices: Array.from(devices.values()).map((device) => ({
      id: device.id,
      token: device.token,
      firstSeenMs: device.firstSeenMs,
    })),
  };
  await writeFile(registryPath, JSON.stringify(body, null, 2), "utf8");
}

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

function getBearerToken(req) {
  const auth = req.headers.authorization || "";
  return auth.startsWith("Bearer ") ? auth.slice(7) : "";
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

function getDevice(id) {
  return typeof id === "string" && id.length > 0 ? devices.get(id) || null : null;
}

async function getOrRegisterDevice(id, token) {
  if (typeof id !== "string" || id.length < 3 || id.length > 64) {
    return { ok: false, status: 400, error: "invalid device id" };
  }
  if (typeof token !== "string" || token.length < 16 || token.length > 128) {
    return { ok: false, status: 401, error: "invalid device token" };
  }

  let device = devices.get(id);
  if (!device) {
    device = createDeviceRecord(id, token);
    devices.set(id, device);
    await saveRegistry();
    console.log(`registered device ${id}`);
    return { ok: true, device, registered: true };
  }
  if (device.token !== token) {
    return { ok: false, status: 401, error: "device token mismatch" };
  }
  return { ok: true, device, registered: false };
}

function isOnline(device) {
  return device.lastSeenMs > 0 && Date.now() - device.lastSeenMs <= offlineAfterMs;
}

function deviceSummary(device) {
  return {
    id: device.id,
    online: isOnline(device),
    last_seen_ms: device.lastSeenMs,
    first_seen_ms: device.firstSeenMs,
    queued_commands: device.commandQueue.length,
    state: device.status?.state || "",
  };
}

function publicStatus(device) {
  return {
    id: device.id,
    online: isOnline(device),
    last_seen_ms: device.lastSeenMs,
    first_seen_ms: device.firstSeenMs,
    status: device.status,
    queued_commands: device.commandQueue.length,
    last_result: device.lastResult,
  };
}

function enqueueCommand(device, type, payload = {}) {
  const command = {
    id: randomUUID(),
    type,
    payload,
    created_at: Date.now(),
  };
  device.commandQueue.push(command);
  return command;
}

function findSelectedDevice(url) {
  const requested = url.searchParams.get("device_id") || "";
  if (requested) {
    return getDevice(requested);
  }
  return Array.from(devices.values())[0] || null;
}

function sendIndex(res) {
  send(res, 200, `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Bell Robot</title>
  <style>
    body{margin:0;background:#101214;color:#eef2f5;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
    main{max-width:820px;margin:0 auto;padding:18px 14px 28px}
    h1{font-size:24px;margin:4px 0 16px}
    section{border-top:1px solid #2b3137;padding:14px 0}
    img{width:100%;height:auto;background:#222;display:block}
    button,input,select{font-size:17px}
    button{padding:10px 14px;margin:6px 6px 6px 0;background:#2d6cdf;color:white;border:0;border-radius:6px}
    input,select{box-sizing:border-box;width:100%;padding:10px;background:#181c20;color:#eef2f5;border:1px solid #424a53;border-radius:6px}
    label{display:block;margin:10px 0 5px;color:#b8c2cc}
    .row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
    .muted{color:#9aa6b2}
    #msg{min-height:24px;color:#9fdb9f}
    pre{white-space:pre-wrap;background:#181c20;padding:10px;border-radius:6px;overflow:auto}
    @media (max-width:640px){.row{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <main>
    <h1>Bell Robot</h1>
    <section>
      <label for="deviceSelect">设备</label>
      <select id="deviceSelect" onchange="selectDevice(this.value)"></select>
      <p id="deviceMsg" class="muted">加载中...</p>
    </section>
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
        <button type="button" onclick="forgetDevice()">删除设备记录</button>
        <span id="msg"></span>
      </form>
    </section>
  </main>
  <script>
    let selectedDeviceId = localStorage.getItem('bellRobotDeviceId') || '';
    let editingSettings = false;
    let settingsDirty = false;

    function markEditing(){
      editingSettings = true;
      settingsDirty = true;
    }
    function stopEditing(){
      editingSettings = false;
    }
    function apiUrl(path){
      const sep = path.includes('?') ? '&' : '?';
      return selectedDeviceId ? path + sep + 'device_id=' + encodeURIComponent(selectedDeviceId) : path;
    }
    function selectDevice(id){
      selectedDeviceId = id;
      localStorage.setItem('bellRobotDeviceId', id);
      settingsDirty = false;
      editingSettings = false;
      document.getElementById('frame').removeAttribute('src');
      loadStatus();
    }
    async function getJson(url, options){
      const r = await fetch(url, options);
      if(!r.ok) throw new Error(await r.text());
      return await r.json();
    }
    async function loadDevices(){
      try{
        const data = await getJson('/api/devices');
        const select = document.getElementById('deviceSelect');
        select.innerHTML = '';
        for (const device of data.devices) {
          const option = document.createElement('option');
          option.value = device.id;
          option.textContent = device.id + (device.online ? ' 在线' : ' 离线');
          select.appendChild(option);
        }
        if (!data.devices.length) {
          const option = document.createElement('option');
          option.value = '';
          option.textContent = '暂无设备';
          select.appendChild(option);
          selectedDeviceId = '';
        } else if (!data.devices.some(d => d.id === selectedDeviceId)) {
          selectedDeviceId = data.devices[0].id;
          localStorage.setItem('bellRobotDeviceId', selectedDeviceId);
        }
        select.value = selectedDeviceId;
        document.getElementById('deviceMsg').textContent = data.devices.length ? '已登记 ' + data.devices.length + ' 台设备' : '等待设备上线登记';
      }catch(e){
        document.getElementById('deviceMsg').textContent = '设备列表读取失败';
      }
    }
    async function loadStatus(){
      if (!selectedDeviceId) {
        document.getElementById('summary').textContent = '暂无设备在线';
        document.getElementById('status').textContent = '';
        return;
      }
      try{
        const data = await getJson(apiUrl('/api/status'));
        const st = data.status || {};
        document.getElementById('summary').textContent = data.online ? '设备在线' : '设备离线';
        document.getElementById('status').textContent = JSON.stringify(st, null, 2);
        if(!editingSettings && !settingsDirty){
          if(st.sit_minutes) document.getElementById('sit').value = st.sit_minutes;
          if(st.away_minutes) document.getElementById('away').value = st.away_minutes;
        }
      }catch(e){
        document.getElementById('summary').textContent = '状态读取失败';
      }
    }
    async function refreshFrame(){
      const msg = document.getElementById('captureMsg');
      if (!selectedDeviceId) {
        msg.textContent = '没有可用设备';
        return;
      }
      msg.textContent = '请求中...';
      try{
        const r = await fetch(apiUrl('/api/capture.jpg?ts=' + Date.now()));
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
      if (!selectedDeviceId) {
        msg.textContent = 'No device';
        return;
      }
      msg.textContent = 'Saving...';
      try{
        await getJson(apiUrl('/api/settings'), {
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body:JSON.stringify({
            sit_minutes:Number(document.getElementById('sit').value),
            away_minutes:Number(document.getElementById('away').value)
          })
        });
        settingsDirty = false;
        editingSettings = false;
        msg.textContent = 'Saved';
      }catch(e){
        msg.textContent = 'Save failed';
      }
    }
    async function resetTimer(){
      const msg = document.getElementById('msg');
      if (!selectedDeviceId) {
        msg.textContent = 'No device';
        return;
      }
      msg.textContent = 'Resetting...';
      try{
        await getJson(apiUrl('/api/reset'), { method:'POST' });
        msg.textContent = 'Reset queued';
      }catch(e){
        msg.textContent = 'Reset failed';
      }
    }
    async function forgetDevice(){
      const msg = document.getElementById('msg');
      if (!selectedDeviceId || !confirm('删除这台设备记录？设备下次上线会重新登记。')) {
        return;
      }
      try{
        await getJson(apiUrl('/api/device'), { method:'DELETE' });
        localStorage.removeItem('bellRobotDeviceId');
        selectedDeviceId = '';
        msg.textContent = 'Deleted';
        await loadDevices();
        await loadStatus();
      }catch(e){
        msg.textContent = 'Delete failed';
      }
    }
    document.addEventListener('DOMContentLoaded', async () => {
      for (const id of ['sit', 'away']) {
        const input = document.getElementById(id);
        input.addEventListener('focus', markEditing);
        input.addEventListener('input', markEditing);
        input.addEventListener('blur', stopEditing);
      }
      await loadDevices();
      await loadStatus();
    });
    setInterval(async () => {
      await loadDevices();
      await loadStatus();
    }, 1000);
  </script>
</body>
</html>`, { "Content-Type": "text/html; charset=utf-8" });
}

function requireDeviceFromUrl(res, url) {
  const device = findSelectedDevice(url);
  if (!device) {
    sendJson(res, 404, { error: "device not found" });
    return null;
  }
  return device;
}

async function handleApi(req, res, url) {
  if (req.method === "GET" && url.pathname === "/") {
    sendIndex(res);
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/devices") {
    sendJson(res, 200, { devices: Array.from(devices.values()).map(deviceSummary) });
    return;
  }
  if (req.method === "DELETE" && url.pathname === "/api/device") {
    const id = url.searchParams.get("device_id") || "";
    if (!devices.delete(id)) {
      sendJson(res, 404, { error: "device not found" });
      return;
    }
    await saveRegistry();
    sendJson(res, 200, { ok: true });
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/status") {
    const device = requireDeviceFromUrl(res, url);
    if (!device) {
      return;
    }
    sendJson(res, 200, publicStatus(device));
    return;
  }
  if (req.method === "POST" && url.pathname === "/api/settings") {
    const device = requireDeviceFromUrl(res, url);
    if (!device) {
      return;
    }
    const body = await readJson(req, 4096);
    const sit = Number(body.sit_minutes);
    const away = Number(body.away_minutes);
    if (!Number.isInteger(sit) || sit < 1 || sit > 180 ||
        !Number.isInteger(away) || away < 1 || away > 5) {
      sendJson(res, 400, { error: "settings out of range" });
      return;
    }
    const command = enqueueCommand(device, "set_settings", { sit_minutes: sit, away_minutes: away });
    sendJson(res, 202, { queued: true, command_id: command.id });
    return;
  }
  if (req.method === "POST" && url.pathname === "/api/reset") {
    const device = requireDeviceFromUrl(res, url);
    if (!device) {
      return;
    }
    const command = enqueueCommand(device, "reset");
    sendJson(res, 202, { queued: true, command_id: command.id });
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/capture.jpg") {
    const device = requireDeviceFromUrl(res, url);
    if (!device) {
      return;
    }
    if (!isOnline(device)) {
      send(res, 503, "device offline", { "Content-Type": "text/plain; charset=utf-8" });
      return;
    }
    const command = enqueueCommand(device, "capture");
    const waitResult = await new Promise((resolve) => {
      const timer = setTimeout(() => {
        device.captureWaiters.delete(command.id);
        resolve(null);
      }, 10000);
      device.captureWaiters.set(command.id, { resolve, timer });
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
  const token = getBearerToken(req);

  if (req.method === "POST" && url.pathname === "/device/poll") {
    const body = await readJson(req, 16384);
    const auth = await getOrRegisterDevice(body.device_id, token);
    if (!auth.ok) {
      sendJson(res, auth.status, { error: auth.error });
      return;
    }
    const device = auth.device;
    device.lastSeenMs = Date.now();
    device.status = body.status || null;
    const command = device.commandQueue.shift() || null;
    sendJson(res, 200, { registered: auth.registered, command });
    return;
  }
  if (req.method === "POST" && url.pathname === "/device/result") {
    const body = await readJson(req, 4096);
    const auth = await getOrRegisterDevice(body.device_id, token);
    if (!auth.ok) {
      sendJson(res, auth.status, { error: auth.error });
      return;
    }
    auth.device.lastResult = {
      command_id: body.command_id || "",
      ok: body.ok === true,
      message: body.message || "",
      ts: Date.now(),
    };
    sendJson(res, 200, { ok: true });
    return;
  }
  if (req.method === "POST" && url.pathname === "/device/capture") {
    const deviceId = url.searchParams.get("device_id") || "";
    const commandId = url.searchParams.get("command_id") || "";
    const auth = await getOrRegisterDevice(deviceId, token);
    if (!auth.ok) {
      sendJson(res, auth.status, { error: auth.error });
      return;
    }
    const waiter = auth.device.captureWaiters.get(commandId);
    if (!waiter) {
      sendJson(res, 404, { error: "capture command not waiting" });
      return;
    }
    const image = await readBody(req, 512 * 1024);
    clearTimeout(waiter.timer);
    auth.device.captureWaiters.delete(commandId);
    waiter.resolve({ image });
    sendJson(res, 200, { ok: true });
    return;
  }
  sendJson(res, 404, { error: "not found" });
}

await loadRegistry();

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);
    if (url.pathname.startsWith("/device/")) {
      await handleDevice(req, res, url);
    } else {
      await handleApi(req, res, url);
    }
  } catch (error) {
    sendJson(res, 500, { error: error instanceof Error ? error.message : "server error" });
  }
});

server.listen(port, () => {
  console.log(`Bell Robot relay listening on ${port}`);
});
