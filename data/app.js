'use strict';
// Shared script for all dashboard pages (selected by <body data-page>).
const $ = (s) => document.querySelector(s);
const page = document.body.dataset.page;

// ---------- helpers ----------
const fmtBytes = (b) => b >= 1048576 ? (b / 1048576).toFixed(2) + ' MB'
                      : b >= 1024 ? (b / 1024).toFixed(1) + ' KB' : b + ' B';
const fmtUptime = (s) => {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600),
        m = Math.floor(s % 3600 / 60), sec = s % 60;
  return (d ? d + 'd ' : '') + [h, m, sec].map((v) => String(v).padStart(2, '0')).join(':');
};
function toast(msg) {
  const t = $('#toast'); if (!t) return;
  t.textContent = msg; t.classList.add('show');
  clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('show'), 3500);
}
async function post(url, body) {
  // X-Savant marks the request as coming from our own pages (see web_server.cpp).
  const r = await fetch(url, { method: 'POST', headers: { 'X-Savant': '1' }, body });
  const j = await r.json().catch(() => ({}));
  if (!r.ok || j.ok === false) throw new Error(j.error || r.status + ' ' + r.statusText);
  return j;
}
function fillKv(sel, rows) {
  const dl = $(sel); if (!dl) return;
  dl.replaceChildren(...rows.flatMap(([k, v]) => {
    const dt = document.createElement('dt'), dd = document.createElement('dd');
    dt.textContent = k; dd.textContent = v; return [dt, dd];
  }));
}
// Two-tap confirm for destructive buttons (no confirm() dialogs).
function armed(btn, label) {
  if (btn.classList.contains('armed')) return true;
  const orig = btn.textContent;
  btn.classList.add('armed'); btn.textContent = label || 'Tap again to confirm';
  setTimeout(() => { btn.classList.remove('armed'); btn.textContent = orig; }, 4000);
  return false;
}
const bars = (rssi) => rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
function sigEl(rssi) {
  const s = document.createElement('span'); s.className = 'sig';
  for (let i = 1; i <= 4; i++) { const b = document.createElement('i'); if (i <= bars(rssi)) b.className = 'on'; s.append(b); }
  return s;
}

// ---------- live status (all pages) ----------
let last = null;
function render(s) {
  last = s;
  const c = $('#conn');
  c.className = 'pill ' + (s.connected ? 'on' : s.apMode ? 'ap' : 'off');
  c.textContent = s.connected ? 'online' : s.apMode ? 'setup' : 'offline';
  renderPage[page]?.(s);
}
const renderPage = {
  home(s) {
    $('#hostname').textContent = s.hostname + '.local';
    $('#ipaddr').textContent = s.ip;
    $('#wifiSub').textContent = s.connected ? `${s.ssid} · ${s.rssi} dBm` : s.apMode ? 'Setup hotspot active' : 'Not connected';
    $('#fwSub').textContent = 'Firmware v' + s.fw;
    fillKv('#info', [
      ['Uptime', fmtUptime(s.uptime)],
      ['Free memory', fmtBytes(s.heapFree)],
      ['Signal', s.connected ? s.rssi + ' dBm' : '–'],
      ['Storage', `${fmtBytes(s.fsUsed)} / ${fmtBytes(s.fsTotal)}`],
      ['Chip', `${s.chip} rev ${s.chipRev}`],
    ]);
  },
  wifi(s) {
    fillKv('#wifiInfo', [
      ['Status', s.connected ? 'Connected' : s.apMode ? 'Setup hotspot' : 'Disconnected'],
      ['Network', s.ssid || '–'],
      ['Signal', s.connected ? s.rssi + ' dBm' : '–'],
      ['IP address', s.ip],
      ['MAC', s.mac],
    ]);
  },
  update(s) {
    fillKv('#fwInfo', [
      ['Firmware', 'v' + s.fw],
      ['Built', s.build],
      ['Running from', s.partition],
      ['Firmware size', fmtBytes(s.sketchSize)],
      ['Max firmware', fmtBytes(s.sketchFree)],
      ['Web files', `${fmtBytes(s.fsUsed)} / ${fmtBytes(s.fsTotal)}`],
    ]);
  },
};

// ---------- machine (live, pushed every 0.5 s) ----------
const STATE_LABEL = { IDLE: 'Idle', BREWING: 'Brewing', STEAM: 'Steam', HOT_WATER: 'Hot water',
                      FAULT: 'Fault', INHIBITED: 'Paused' };
function renderMachine(m) {
  if (page !== 'home') return;
  graph.push(m);
  const st = $('#mState');
  st.textContent = STATE_LABEL[m.state] || m.state;
  st.className = 'state ' + m.state.toLowerCase();

  let alert = '', bad = false;
  if (m.fault) { alert = m.fault + ' — power-cycle the machine to clear.'; bad = true; }
  else if (m.inhibit) alert = 'Heater and pump paused: ' + m.inhibit + '.';
  else if (m.pumpTimedOut) alert = 'Pump stopped after the run-time limit. Turn the Coffee switch off to reset.';
  else if (m.waitSwitchCycle) alert = 'Coffee switch was already on. Turn it off and on again to start the pump.';
  const a = $('#mAlert');
  a.hidden = !alert; a.textContent = alert; a.classList.toggle('bad', bad);

  const ready = m.coffeeLight || m.steamLight;
  $('#mTemp').textContent = m.temp == null ? '–' : (+m.temp).toFixed(1) + '°';
  $('#mTemp').classList.toggle('ok', ready);
  $('#mSet').textContent = 'target ' + (+m.setpoint).toFixed(1) + '°';
  $('#mPress').textContent = m.pressure == null ? '–' : (+m.pressure).toFixed(1);
  $('#mPressT').textContent = m.pressureTarget ? 'target ' + (+m.pressureTarget).toFixed(1) + ' bar' : 'bar';
  $('#mShot').textContent = m.shotMs ? (m.shotMs / 1000).toFixed(1) : '–';

  const on = (b) => b ? 'On' : 'Off';
  fillKv('#mInfo', [
    ['Heater', `${(+m.heaterDuty).toFixed(0)} %`],
    ['Pump', `${m.pump} %`],
    ['Valve', m.valve ? 'Open' : 'Closed'],
    ['Switches', `Coffee ${on(m.coffeeSw)} · Steam ${on(m.steamSw)}`],
    ['Mains', `${(+m.mainsHz).toFixed(1)} Hz`],
  ]);
}

// ---------- machine graph (home page, Machine card) ----------
// Scrolls right to left: the right edge is "now". Left axis °C (auto-scaled),
// right axis bar (fixed 0-12, the sensor range). Gridlines are shared by both.
const graph = (() => {
  if (page !== 'home') return { push() {} };
  const cv = $('#mGraph'), ctx = cv.getContext('2d');
  const KEEP_S = 600, GAP_S = 4, DIVS = 6, P_MAX = 12;
  let windowS = 60, data = [], colors = {}, w = 0, h = 0, dpr = 1, lastDraw = 0;

  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const readColors = () => {
    colors = { temp: css('--g-temp'), press: css('--g-press'), grid: css('--line'), text: css('--muted') };
  };
  readColors();
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', readColors);

  // Setting canvas.width/height clears it, so only do it on a real size change, then redraw.
  const resize = () => {
    const nd = window.devicePixelRatio || 1, nw = cv.clientWidth, nh = cv.clientHeight;
    if (nd === dpr && nw === w && nh === h) return;
    dpr = nd; w = nw; h = nh;
    cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
    draw();
  };
  new ResizeObserver(resize).observe(cv);
  resize();

  fetch('/api/settings').then((r) => r.json())
    .then((j) => { windowS = +j.values.graphWindowS || 60; }).catch(() => {});

  function push(m) {
    const t = performance.now() / 1000;
    data.push({
      t,
      temp: m.temp == null ? null : +m.temp,
      set: +m.setpoint,
      press: m.pressure == null ? null : +m.pressure,
      pset: m.pressureTarget ? +m.pressureTarget : null,   // only while brewing
    });
    while (data.length && data[0].t < t - KEEP_S) data.shift();
  }

  // Temperature axis: DIVS equal steps of a "nice" size covering lo..hi.
  function tempAxis(lo, hi) {
    for (const st of [1, 2, 5, 10, 20, 25, 50]) {
      const a = Math.floor(lo / st) * st;
      if (a + DIVS * st >= hi) return { lo: a, st };
    }
    return { lo: 0, st: 50 };
  }

  function draw() {
    const now = performance.now() / 1000, t0 = now - windowS;
    const L = 36, R = 28, T = 8, B = 18, pw = w - L - R, ph = h - T - B;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    if (pw <= 0 || ph <= 0) return;

    let lo = Infinity, hi = -Infinity;
    for (const d of data) {
      if (d.t < t0) continue;
      for (const v of [d.temp, d.set]) if (v != null) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    }
    if (!isFinite(lo)) { lo = 90; hi = 110; }
    const ta = tempAxis(lo - 2, hi + 2);
    const X = (t) => L + (t - t0) / windowS * pw;
    const YT = (v) => T + ph - (v - ta.lo) / (ta.st * DIVS) * ph;
    const YP = (v) => T + ph - v / P_MAX * ph;

    // horizontal grid + axis labels (°C left, bar right)
    ctx.font = '11px system-ui, sans-serif';
    ctx.lineWidth = 1;
    ctx.textBaseline = 'middle';
    for (let i = 0; i <= DIVS; i++) {
      const y = Math.round(T + ph - i / DIVS * ph) + 0.5;
      ctx.strokeStyle = colors.grid;
      ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + pw, y); ctx.stroke();
      ctx.fillStyle = colors.temp; ctx.textAlign = 'right';
      ctx.fillText(ta.lo + i * ta.st, L - 5, y);
      ctx.fillStyle = colors.press; ctx.textAlign = 'left';
      ctx.fillText(i * P_MAX / DIVS, L + pw + 5, y);
    }
    // vertical time grid, labelled in seconds ago
    const step = [5, 10, 15, 30, 60, 120].find((s) => windowS / s <= 6) || 120;
    ctx.fillStyle = colors.text; ctx.textAlign = 'center'; ctx.textBaseline = 'top';
    for (let s = 0; s <= windowS; s += step) {
      const x = Math.round(L + pw - s / windowS * pw) + 0.5;
      ctx.strokeStyle = colors.grid;
      ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + ph); ctx.stroke();
      ctx.fillText(s ? `-${s}s` : 'now', Math.min(Math.max(x, L + 12), L + pw - 12), T + ph + 4);
    }

    // series, clipped to the plot area; gaps where data is missing
    ctx.save();
    ctx.beginPath(); ctx.rect(L, T, pw, ph); ctx.clip();
    const line = (key, Y, color, dash) => {
      ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.setLineDash(dash); ctx.lineJoin = 'round';
      ctx.beginPath();
      let pen = false, prevT = -Infinity;
      for (const d of data) {
        const v = d[key];
        if (v == null || d.t - prevT > GAP_S) pen = false;
        prevT = d.t;
        if (v == null || d.t < t0 - GAP_S) continue;
        const x = X(d.t), y = Y(v);
        pen ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        pen = true;
      }
      ctx.stroke();
    };
    line('set', YT, colors.temp, [6, 4]);
    line('pset', YP, colors.press, [6, 4]);
    line('temp', YT, colors.temp, []);
    line('press', YP, colors.press, []);
    ctx.restore();
    ctx.setLineDash([]);
  }

  // Redraw ~30 fps for smooth scrolling (requestAnimationFrame pauses in background tabs).
  const frame = (ts) => {
    if (ts - lastDraw >= 33) { lastDraw = ts; draw(); }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
  return { push };
})();

let pollTimer;
const pollOnce = () => {
  fetch('/api/status').then((r) => r.json()).then(render).catch(() => {});
  if (page === 'home') fetch('/api/machine').then((r) => r.json()).then(renderMachine).catch(() => {});
};
function connectWs() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (e) => {
    try {
      const j = JSON.parse(e.data);
      j.type === 'machine' ? renderMachine(j) : render(j);
    } catch (_) {}
  };
  ws.onopen = () => clearInterval(pollTimer);
  ws.onclose = () => {
    $('#conn').className = 'pill off'; $('#conn').textContent = 'offline';
    clearInterval(pollTimer);   // poll while the socket is retrying
    pollTimer = setInterval(pollOnce, 3000);
    setTimeout(connectWs, 3000);
  };
}
connectWs();

// ---------- Wi-Fi page ----------
if (page === 'wifi') {
  const scanBtn = $('#scanBtn');
  async function scan() {
    scanBtn.disabled = true; scanBtn.textContent = 'Scanning…';
    let list = [];
    for (let i = 0; i < 10 && !list.length; i++) {   // scan runs async on the device
      list = await fetch('/api/wifi/scan').then((r) => r.json()).catch(() => []);
      if (!list.length) await new Promise((r) => setTimeout(r, 1000));
    }
    showNetworks(list);
    scanBtn.disabled = false; scanBtn.textContent = 'Scan';
  }
  function showNetworks(list) {
    const best = new Map();
    for (const n of list) if (n.ssid && (!best.has(n.ssid) || best.get(n.ssid).rssi < n.rssi)) best.set(n.ssid, n);
    const ul = $('#networks'); ul.replaceChildren();
    if (!best.size) { ul.innerHTML = '<li class="muted">No networks found</li>'; return; }
    for (const n of [...best.values()].sort((a, b) => b.rssi - a.rssi)) {
      const li = document.createElement('li'); li.className = 'net';
      const name = document.createElement('span'); name.className = 'name'; name.textContent = n.ssid;
      const lock = document.createElement('span'); lock.className = 'lock'; lock.textContent = n.secure ? '🔒' : '';
      li.append(name, lock, sigEl(n.rssi));
      li.onclick = () => {
        $('#ssidIn').value = n.ssid;
        $('#wifiForm').scrollIntoView({ behavior: 'smooth', block: 'center' });
        $('#passIn').focus({ preventScroll: true });
      };
      ul.append(li);
    }
  }
  scanBtn.onclick = scan;
  scan();

  $('#showPass').onchange = (e) => { $('#passIn').type = e.target.checked ? 'text' : 'password'; };
  $('#wifiForm').onsubmit = async (e) => {
    e.preventDefault();
    try {
      await post('/api/wifi', new URLSearchParams(new FormData(e.target)));
      toast('Saved. Rebooting – reconnect your phone to that network.');
    } catch (err) { toast('Error: ' + err.message); }
  };
  $('#forgetBtn').onclick = async (e) => {
    if (!armed(e.target)) return;
    try { await post('/api/wifi/forget'); toast('Forgotten. Rebooting into setup hotspot…'); }
    catch (err) { toast('Error: ' + err.message); }
  };
}

// ---------- Update page ----------
if (page === 'update') {
  const form = $('#otaForm'), fileLbl = $('#fileName').parentElement;
  const hints = {
    firmware: 'From .pio/build/esp32-serial/firmware.bin',
    fs: 'From .pio/build/esp32-serial/littlefs.bin (pio run -t buildfs)',
  };
  const setHint = () => { $('#hint').textContent = hints[form.elements.target.value]; };
  form.querySelectorAll('[name=target]').forEach((r) => r.onchange = setHint);
  setHint();
  form.file.onchange = () => {
    const f = form.file.files[0];
    $('#fileName').textContent = f ? `${f.name} (${fmtBytes(f.size)})` : 'Choose .bin file…';
    fileLbl.classList.toggle('has', !!f);
  };

  form.onsubmit = (e) => {
    e.preventDefault();
    const file = form.file.files[0]; if (!file) return;
    if (form.elements.target.value === 'firmware' && last && file.size > last.sketchFree) {
      $('#otaMsg').textContent = `File too large (${fmtBytes(file.size)} > ${fmtBytes(last.sketchFree)})`;
      return;
    }
    const btn = form.querySelector('button'), bar = $('#otaBar'), msg = $('#otaMsg');
    const fd = new FormData(); fd.append('file', file, file.name);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update?target=' + form.elements.target.value);
    xhr.setRequestHeader('X-Savant', '1');
    xhr.upload.onprogress = (ev) => {
      if (!ev.lengthComputable) return;
      const p = Math.round(100 * ev.loaded / ev.total);
      bar.style.width = p + '%';
      msg.textContent = p < 100 ? `Uploading… ${p}%` : 'Writing to flash…';
    };
    xhr.onload = () => {
      let j = {}; try { j = JSON.parse(xhr.responseText); } catch (_) {}
      if (xhr.status === 200 && j.ok) {
        msg.textContent = 'Update successful – rebooting…';
        setTimeout(() => location.href = '/', 8000);
      } else {
        msg.textContent = 'Update failed: ' + (j.error || xhr.status + ' ' + xhr.statusText);
        btn.disabled = false;
      }
    };
    xhr.onerror = () => { msg.textContent = 'Upload error – connection lost'; btn.disabled = false; };
    btn.disabled = true; bar.style.width = '0';
    xhr.send(fd);
  };

  $('#rebootBtn').onclick = async (e) => {
    if (!armed(e.target)) return;
    try { await post('/api/reboot'); toast('Rebooting…'); setTimeout(() => location.href = '/', 6000); }
    catch (err) { toast('Error: ' + err.message); }
  };
}

// ---------- Dashboard: IO tile subtitle ----------
if (page === 'home') {
  fetch('/api/io').then((r) => r.json()).then((j) => {
    const used = j.pins.filter((p) => p.type !== 'Unused').length;
    $('#ioSub').textContent = `${used} of ${j.pins.length} pins in use`;
  }).catch(() => {});
}

// ---------- IO Status page ----------
if (page === 'io') {
  const ul = $('#pins');
  const open = new Set();          // GPIOs whose notes are expanded
  let pins = [], filter = 'all';
  const matches = (p, f) => f === 'all' || (f === 'used' ? p.type !== 'Unused' : p.type === f);

  const el = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  };

  function stateEl(p) {
    const st = el('div', 'st');
    if (p.mv != null) {
      const v = el('div', 'av', (p.mv / 1000).toFixed(2) + ' V');
      if (p.bar != null) v.append(el('small', null, (+p.bar).toFixed(2) + ' bar'));
      else if (p.raw != null) v.append(el('small', null, 'raw ' + p.raw));
      st.append(v);
    } else if (p.level != null) {
      st.append(el('span', 'lv ' + (p.level ? 'hi' : 'lo'), p.level ? 'HIGH' : 'LOW'));
    } else {
      st.append(el('span', 'lv na', '–'));
    }
    return st;
  }

  function draw() {
    document.querySelectorAll('#filters .chip').forEach((c) => {
      c.querySelector('b').textContent = pins.filter((p) => matches(p, c.dataset.f)).length;
    });
    const shown = pins.filter((p) => matches(p, filter));
    if (!shown.length) { ul.replaceChildren(el('li', 'muted', 'No pins in this group')); return; }
    ul.replaceChildren(...shown.map((p) => {
      const li = el('li'); if (open.has(p.gpio)) li.classList.add('open');
      const row = el('div', 'pin' + (p.type === 'Unused' ? ' unused' : ''));
      const gp = el('div', 'gp'); gp.append(el('small', null, 'GPIO'), document.createTextNode(p.gpio));
      const pn = el('div', 'pn');
      pn.append(el('div', 'nm', p.name));
      const meta = el('div', 'meta');
      meta.append(el('span', 'ty ' + p.type, p.type === 'Communications' ? 'Comms' : p.type));
      meta.append(el('span', null, p.mode));
      if (p.pull) meta.append(el('span', null, 'pull-' + p.pull));
      if (p.note) meta.append(el('span', 'warn', p.note));
      pn.append(meta);
      row.append(gp, pn, stateEl(p));
      const notes = el('div', 'pnotes');
      notes.append(el('div', null, p.notes || 'No notes'));
      const mc = el('div'); mc.append('Macro: ', el('code', null, p.macro)); notes.append(mc);
      li.append(row, notes);
      row.onclick = () => { open.has(p.gpio) ? open.delete(p.gpio) : open.add(p.gpio); li.classList.toggle('open'); };
      return li;
    }));
  }

  document.querySelectorAll('#filters .chip').forEach((c) => c.onclick = () => {
    document.querySelectorAll('#filters .chip').forEach((x) => x.classList.toggle('active', x === c));
    filter = c.dataset.f; draw();
  });

  let timer;
  async function poll() {
    clearTimeout(timer);
    try {
      const j = await fetch('/api/io').then((r) => r.json());
      pins = j.pins; draw();
    } catch (_) { /* keep last values; connection pill shows offline */ }
    if (!document.hidden) timer = setTimeout(poll, 1000);
  }
  document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
  poll();
}

// ---------- Settings page ----------
if (page === 'settings') {
  const form = $('#setForm');
  function fill(j) {
    for (const [k, v] of Object.entries(j.values)) {
      const inp = form.elements[k]; if (!inp) continue;
      inp.value = +v.toFixed(3);
      const [lo, hi] = j.limits[k] || [];
      if (lo != null) { inp.min = lo; inp.max = hi; }
      inp.title = `Default ${j.defaults[k]}` + (lo != null ? ` · range ${lo}–${hi}` : '');
    }
  }
  fetch('/api/settings').then((r) => r.json()).then(fill).catch(() => toast('Could not load settings'));

  form.onsubmit = async (e) => {
    e.preventDefault();
    try { fill(await post('/api/settings', new URLSearchParams(new FormData(form)))); toast('Saved'); }
    catch (err) { toast('Error: ' + err.message); }
  };
  $('#resetBtn').onclick = async (e) => {
    if (!armed(e.target)) return;
    try { fill(await post('/api/settings/reset')); toast('Defaults restored'); }
    catch (err) { toast('Error: ' + err.message); }
  };
}
