/* Playback data is sent only to YT-Chatterino on this computer. */
const api = globalThis.browser || globalThis.chrome;
const states = new Map();
const client = crypto.randomUUID();
let socket = null, retry = null, status = "Waiting for the app";
function vodUrl(raw) {
  let u; try { u = new URL(raw); } catch (_) { return ""; }
  if (u.protocol !== "https:" || u.username || u.password || (u.port && u.port !== "443")) return "";
  const host = u.hostname.toLowerCase();
  if (["www.youtube.com", "youtube.com", "m.youtube.com"].includes(host) && u.pathname === "/watch") {
    const id = u.searchParams.get("v") || "";
    return /^[A-Za-z0-9_-]{11}$/.test(id) ? "https://www.youtube.com/watch?v=" + id : "";
  }
  if (["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(host)) {
    const match = u.pathname.match(/^\/videos\/([0-9]{1,20})\/?$/);
    return match ? "https://www.twitch.tv/videos/" + match[1] : "";
  }
  if (["kick.com", "www.kick.com"].includes(host)) {
    const match = u.pathname.match(/^\/([A-Za-z0-9_-]{1,80})\/videos\/([a-f0-9-]{36})\/?$/i);
    return match ? "https://kick.com/" + match[1] + "/videos/" + match[2] : "";
  }
  return "";
}
function send(data) {
  if (socket?.readyState === WebSocket.OPEN && status === "Connected") socket.send(JSON.stringify(data));
}
function connect() {
  if (socket || !states.size) return;
  clearTimeout(retry); retry = null; status = "Connecting…";
  let ws; try { ws = new WebSocket("ws://127.0.0.1:37367/"); } catch (_) { status = "Waiting for the app"; return; }
  socket = ws;
  ws.onopen = () => ws.send(JSON.stringify({type:"hello", client}));
  ws.onmessage = event => {
    let data; try { data = JSON.parse(event.data); } catch (_) { return; }
    if (socket !== ws || data.type !== "ready") return;
    status = "Connected";
    for (const state of states.values()) send({...state, seek:true});
  };
  ws.onerror = () => { status = "Waiting for the app"; };
  ws.onclose = () => {
    if (socket !== ws) return;
    socket = null; status = "Waiting for the app";
    if (states.size) retry = setTimeout(connect, 3000);
  };
}
function closeTab(tab) {
  if (!states.delete(tab)) return;
  send({type:"closed", tab});
  if (!states.size) {
    clearTimeout(retry); retry = null;
    if (socket) { const ws=socket; socket=null; ws.onclose=null; ws.close(); }
    status = "Waiting for a VOD";
  }
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  if (message.type === "vod-status" && sender.url === api.runtime.getURL("popup.html")) {
    respond({status, count:states.size}); return;
  }
  if (!sender.tab || sender.frameId !== 0 || !Number.isInteger(sender.tab.id)) return;
  const tab = sender.tab.id;
  if (message.type === "vod-close") { closeTab(tab); return; }
  if (message.type !== "vod-state") return;
  let pageOrigin, currentOrigin;
  try { pageOrigin = new URL(sender.url).origin; currentOrigin = new URL(message.url).origin; } catch (_) { closeTab(tab); return; }
  const url = pageOrigin === currentOrigin ? vodUrl(message.url) : "", seconds = Number(message.seconds);
  if (!url || !Number.isFinite(seconds) || seconds < 0 || seconds > 604800 || typeof message.paused !== "boolean") {
    closeTab(tab); return;
  }
  const previous = states.get(tab);
  const state = {type:"state", tab, url, seconds, paused:message.paused, seek:message.seek === true || previous?.url !== url};
  states.set(tab, state); connect(); send(state);
  if (state.seek) states.set(tab, {...state, seek:false});
});
api.tabs.onRemoved.addListener(closeTab);

api.tabs.onUpdated.addListener((tab, info) => { if (info.status === "loading") closeTab(tab); });
