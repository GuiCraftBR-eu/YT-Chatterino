/* Playback data is sent only to YT-Chatterino on this computer. */
const api = globalThis.browser || globalThis.chrome;
const states = new Map();
const liveStates = new Map();
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
function liveUrl(raw) {
  let u; try { u = new URL(raw); } catch (_) { return ""; }
  if (u.protocol !== "https:" || u.username || u.password || (u.port && u.port !== "443")) return "";
  const host = u.hostname.toLowerCase();
  if (["www.youtube.com", "youtube.com", "m.youtube.com"].includes(host)) {
    const match = u.pathname.match(/^\/live\/([A-Za-z0-9_-]{11})\/?$/);
    const id = u.pathname === "/watch" ? (u.searchParams.get("v") || "") : (match?.[1] || "");
    return /^[A-Za-z0-9_-]{11}$/.test(id) ? "https://www.youtube.com/watch?v=" + id : "";
  }
  const twitch = ["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(host);
  const kick = ["www.kick.com", "kick.com"].includes(host);
  if (!twitch && !kick) return "";
  const match = u.pathname.match(twitch ? /^\/([A-Za-z0-9_]{1,25})\/?$/ : /^\/([A-Za-z0-9_-]{1,80})\/?$/);
  if (!match || ["directory","downloads","jobs","login","p","settings","signup","subscriptions","turbo","videos","wallet","categories","browse","search"].includes(match[1].toLowerCase())) return "";
  return (twitch ? "https://www.twitch.tv/" : "https://kick.com/") + match[1].toLowerCase();
}
function send(data) {
  if (socket?.readyState === WebSocket.OPEN && status === "Connected") socket.send(JSON.stringify(data));
}
function connect() {
  if (socket || (!states.size && !liveStates.size)) return;
  clearTimeout(retry); retry = null; status = "Connecting…";
  let ws; try { ws = new WebSocket("ws://127.0.0.1:37367/"); } catch (_) { status = "Waiting for the app"; return; }
  socket = ws;
  ws.onopen = () => ws.send(JSON.stringify({type:"hello", client}));
  ws.onmessage = event => {
    let data; try { data = JSON.parse(event.data); } catch (_) { return; }
    if (socket !== ws || data.type !== "ready") return;
    status = "Connected";
    for (const state of states.values()) send({...state, seek:true});
    for (const state of liveStates.values()) send(state);
  };
  ws.onerror = () => { status = "Waiting for the app"; };
  ws.onclose = () => {
    if (socket !== ws) return;
    socket = null; status = "Waiting for the app";
    if (states.size || liveStates.size) retry = setTimeout(connect, 3000);
  };
}
function closeTab(tab) {
  const hadVod = states.delete(tab), hadLive = liveStates.delete(tab);
  if (!hadVod && !hadLive) return;
  send({type:"closed", tab});
  if (!states.size && !liveStates.size) {
    clearTimeout(retry); retry = null;
    if (socket) { const ws=socket; socket=null; ws.onclose=null; ws.close(); }
    status = "Waiting for a stream";
  }
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  if (message.type === "vod-status" && sender.url === api.runtime.getURL("popup.html")) {
    respond({status, count:new Set([...states.keys(), ...liveStates.keys()]).size}); return;
  }
  if (!sender.tab || sender.frameId !== 0 || !Number.isInteger(sender.tab.id)) return;
  const tab = sender.tab.id;
  if (message.type === "vod-close") { closeTab(tab); return; }
  if (message.type === "vod-end" || message.type === "live-end") {
    const map = message.type === "vod-end" ? states : liveStates;
    if (map.delete(tab)) send({type:message.type === "vod-end" ? "vod-closed" : "live-closed", tab});
    if (!states.size && !liveStates.size) { clearTimeout(retry); retry=null; if(socket){const ws=socket;socket=null;ws.onclose=null;ws.close();} status="Waiting for a stream"; }
    return;
  }
  if (message.type === "live-state") {
    let pageOrigin, currentOrigin;
    try { pageOrigin=new URL(sender.url).origin; currentOrigin=new URL(message.url).origin; } catch (_) { return; }
    const url=pageOrigin === currentOrigin ? liveUrl(message.url) : "";
    if (!url) return;
    const name=typeof message.name === "string" ? message.name.trim().slice(0,100) : "";
    const state={type:"live-state",tab,url,live:message.live === true && !!name,name}; liveStates.set(tab,state); connect(); send(state); return;
  }
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
