/* Playback state and visible chat panels are sent only to the app on this computer. */
const api = globalThis.browser || globalThis.chrome;
const states = new Map();
const liveStates = new Map();
const youtubeFrames = new Map();
const pendingVotes = new Set();
const pendingPolls = new Set();
const client = crypto.randomUUID();
const endpoints = [37367, 37368].map(port => ({port, socket:null, retry:null, ready:false}));
function status() {
  if (endpoints.some(endpoint => endpoint.ready)) return "Connected";
  if (!states.size && !liveStates.size) return "Waiting for a stream";
  return endpoints.some(endpoint => endpoint.socket) ? "Connecting…" : "Waiting for the app";
}
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
  const match = u.pathname.match(twitch ? /^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/ : /^\/([A-Za-z0-9_-]{1,80})\/?$/);
  if (!match || ["directory","downloads","jobs","login","p","settings","signup","subscriptions","turbo","videos","wallet","categories","browse","search"].includes(match[1].toLowerCase())) return "";
  return (twitch ? "https://www.twitch.tv/" : "https://kick.com/") + match[1].toLowerCase();
}
function youtubeChatId(raw) {
  let url; try { url = new URL(raw); } catch (_) { return ""; }
  if (url.protocol !== "https:" || url.username || url.password || (url.port && url.port !== "443") ||
      !["www.youtube.com", "youtube.com", "m.youtube.com"].includes(url.hostname.toLowerCase()) || url.pathname !== "/live_chat") return "";
  const id = url.searchParams.get("v") || "";
  return /^[A-Za-z0-9_-]{11}$/.test(id) ? id : "";
}
function panel(value) {
  return value && typeof value === "object" && typeof value.text === "string"
    ? {text:value.text.slice(0,800),name:typeof value.name === "string" ? value.name.slice(0,100) : "",
       options:typeof value.options === "string" ? value.options.slice(0,800) : "",
       state:["open","voted","ended","unavailable"].includes(value.state) ? value.state : "unavailable",
       choices:Array.isArray(value.choices) ? value.choices.slice(0,10).filter(c => c &&
         typeof c.id === "string" && /^[a-z0-9_-]{1,128}$/i.test(c.id) &&
         typeof c.label === "string" && c.label.length > 0 && c.label.length <= 100)
         .map(c => ({id:c.id,label:c.label})) : []} : {};
}
function sendTo(endpoint, data) {
  if (endpoint.socket?.readyState === WebSocket.OPEN && endpoint.ready) endpoint.socket.send(JSON.stringify(data));
}
function send(data) { for (const endpoint of endpoints) sendTo(endpoint, data); }
function connectEndpoint(endpoint) {
  if (endpoint.socket || endpoint.retry || (!states.size && !liveStates.size)) return;
  let ws;
  try { ws = new WebSocket(`ws://127.0.0.1:${endpoint.port}/`); }
  catch (_) { endpoint.retry=setTimeout(()=>{endpoint.retry=null;connectEndpoint(endpoint);},3000); return; }
  endpoint.socket = ws;
  ws.onopen = () => ws.send(JSON.stringify({type:"hello", client}));
  ws.onmessage = event => {
    let data; try { data = JSON.parse(event.data); } catch (_) { return; }
    if (endpoint.socket !== ws) return;
    if (data.type === "sync-request" && endpoint.ready) {
      for (const state of states.values()) sendTo(endpoint, {...state, seek:false});
      for (const state of liveStates.values()) sendTo(endpoint, state);
      for (const tab of new Set([...states.keys(), ...liveStates.keys()]))
        api.tabs.sendMessage(tab,{type:"sync-now"}).catch(()=>{});
      return;
    }
    if (data.type === "open-panel" && endpoint.ready) {
      const tab=data.tab, channel=typeof data.channel === "string" ? data.channel.toLowerCase() : "";
      if (!Number.isInteger(tab) || !/^[a-z0-9_]{1,25}$/.test(channel) ||
          !["poll","prediction"].includes(data.kind) || liveStates.get(tab)?.url !== "https://www.twitch.tv/"+channel) return;
      api.tabs.update(tab,{active:true}).then(info=>api.windows.update(info.windowId,{focused:true})).catch(()=>{});
      api.tabs.sendMessage(tab,{type:"open-panel",kind:data.kind,channel}).catch(()=>{});
      return;
    }
    if (data.type === "vote-poll" && endpoint.ready) {
      const tab=data.tab;
      const platform=data.platform || "twitch";
      const channel=typeof data.channel === "string" ? (platform === "youtube" ? data.channel : data.channel.toLowerCase()) : "";
      const request=typeof data.request === "string" ? data.request : "";
      const allowed=platform === "twitch" ? /^[a-z0-9_]{1,25}$/.test(channel) :
        platform === "kick" ? /^[a-z0-9_-]{1,80}$/.test(channel) :
        platform === "youtube" && /^[A-Za-z0-9_-]{11}$/.test(channel);
      if (!Number.isInteger(tab) || !allowed ||
          !/^[a-f0-9-]{36}$/i.test(request) || pendingVotes.has(request)) return;
      let ownsPoll=false;
      const finish = status => {
        pendingVotes.delete(request);
        if (ownsPoll) pendingPolls.delete(pollKey);
        sendTo(endpoint, {type:"poll-result",request,tab,status});
      };
      const pollKey=platform + ":" + tab + ":" + data.question;
      const state=liveStates.get(tab);
      const origin=platform === "kick" ? "https://kick.com/" : platform === "youtube" ?
        "https://www.youtube.com/watch?v=" : "https://www.twitch.tv/";
      if (!state || state.url !== origin+channel) { finish("tab_closed"); return; }
      const frame=youtubeFrames.get(tab);
      if (platform === "youtube" && (!frame || frame.video !== channel || Date.now()-frame.at > 5000)) {
        finish("tab_closed"); return;
      }
      const poll=state.chat?.poll;
      if (!poll || poll.text !== data.question) { finish("changed"); return; }
      if (poll.state === "ended") { finish("ended"); return; }
      if (poll.state === "voted") { finish("already_voted"); return; }
      if (poll.state !== "open" || !poll.choices?.some(c => c.id === data.choice && c.label === data.label)) {
        finish("unavailable"); return;
      }
      // Separate app windows can address the same tab; do not submit twice.
      if (pendingPolls.has(pollKey)) { sendTo(endpoint, {type:"poll-result",request,tab,status:"unavailable"}); return; }
      pendingVotes.add(request);
      pendingPolls.add(pollKey);
      ownsPoll=true;
      const message={type:"vote-poll",platform,channel,question:poll.text,choice:data.choice,label:data.label};
      const action=platform === "youtube" ? api.tabs.sendMessage(tab,message,{frameId:frame.frameId}) : api.tabs.sendMessage(tab,message);
      action
        .then(result => finish(["confirmed","already_voted","ended","changed","unavailable","uncertain"].includes(result?.status)
          ? result.status : "uncertain"), () => finish("tab_closed"));
      return;
    }
    if (data.type !== "ready") return;
    endpoint.ready = true;
    for (const state of states.values()) sendTo(endpoint, {...state, seek:true});
    for (const state of liveStates.values()) sendTo(endpoint, state);
  };
  ws.onerror = () => { endpoint.ready = false; };
  ws.onclose = () => {
    if (endpoint.socket !== ws) return;
    endpoint.socket = null; endpoint.ready = false;
    if (states.size || liveStates.size) endpoint.retry = setTimeout(() => {
      endpoint.retry = null; connectEndpoint(endpoint);
    }, 3000);
  };
}
function connect() { for (const endpoint of endpoints) connectEndpoint(endpoint); }
function disconnectAll() {
  for (const endpoint of endpoints) {
    clearTimeout(endpoint.retry); endpoint.retry=null; endpoint.ready=false;
    if (endpoint.socket) { const ws=endpoint.socket; endpoint.socket=null; ws.onclose=null; ws.close(); }
  }
}
function closeTab(tab) {
  youtubeFrames.delete(tab);
  const hadVod = states.delete(tab), hadLive = liveStates.delete(tab);
  if (!hadVod && !hadLive) return;
  send({type:"closed", tab});
  if (!states.size && !liveStates.size) {
    disconnectAll();
  }
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  if (message.type === "vod-status" && sender.url === api.runtime.getURL("popup.html")) {
    respond({status:status(), count:new Set([...states.keys(), ...liveStates.keys()]).size}); return;
  }
  if (!sender.tab || !Number.isInteger(sender.tab.id)) return;
  const tab = sender.tab.id;
  if (message.type === "youtube-chat-state") {
    if (!Number.isInteger(sender.frameId) || sender.frameId <= 0) return;
    const video=youtubeChatId(sender.url);
    if (!video || youtubeChatId(message.url) !== video) return;
    const state=liveStates.get(tab);
    if (!state || state.url !== "https://www.youtube.com/watch?v=" + video ||
        (sender.tab.url && liveUrl(sender.tab.url) !== state.url)) return;
    const chat={available:true,poll:panel(message.poll)};
    youtubeFrames.set(tab,{video,frameId:sender.frameId,chat,at:Date.now()});
    const updated={...state,chat}; liveStates.set(tab,updated); send(updated); return;
  }
  if (sender.frameId !== 0) return;
  if (message.type === "vod-close") { closeTab(tab); return; }
  if (message.type === "vod-end" || message.type === "live-end") {
    if (message.type === "live-end") youtubeFrames.delete(tab);
    const map = message.type === "vod-end" ? states : liveStates;
    if (map.delete(tab)) send({type:message.type === "vod-end" ? "vod-closed" : "live-closed", tab});
    if (!states.size && !liveStates.size) disconnectAll();
    return;
  }
  if (message.type === "live-state") {
    let pageOrigin, currentOrigin;
    try { pageOrigin=new URL(sender.url).origin; currentOrigin=new URL(message.url).origin; } catch (_) { return; }
    // Content scripts survive same-origin SPA navigation; sender.url can still
    // refer to the page on which the script was originally injected.
    const url=pageOrigin === currentOrigin ? liveUrl(message.url) : "";
    if (!url) return;
    const name=typeof message.name === "string" ? message.name.trim().slice(0,100) : "";
    const state={type:"live-state",tab,url,live:message.live === true && !!name,name};
    if ((url.startsWith("https://www.twitch.tv/") || url.startsWith("https://kick.com/")) && message.chat && typeof message.chat === "object") {
      state.chat={available:message.chat.available === true,pin:panel(message.chat.pin),
        poll:panel(message.chat.poll),prediction:panel(message.chat.prediction)};
      youtubeFrames.delete(tab);
    } else if (url.startsWith("https://www.youtube.com/watch?v=")) {
      const frame=youtubeFrames.get(tab);
      if (frame && url === "https://www.youtube.com/watch?v=" + frame.video && Date.now()-frame.at <= 5000) state.chat=frame.chat;
      else youtubeFrames.delete(tab);
    } else {
      youtubeFrames.delete(tab);
    }
    liveStates.set(tab,state); connect(); send(state); return;
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
