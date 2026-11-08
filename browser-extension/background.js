/* Only the selected tab's URL and video playback position are sent to localhost. */
const api = globalThis.browser || globalThis.chrome;
let connection = null, ws = null, reconnectTimer = null, lastError = "Not connected";
let lastState = null;
const validCode = value => /^(?:[1-9][0-9]{0,4}):[a-f0-9]{64}$/i.test(value) && Number(value.split(":")[0]) <= 65535;
function disconnect(clear) {
  clearTimeout(reconnectTimer); reconnectTimer = null;
  if (ws) { const old = ws; ws = null; old.onclose = null; old.close(); }
  if (clear) { connection = null; lastState = null; lastError = "Not connected"; api.storage.local.remove("vodConnection"); }
}
function start() {
  if (!connection || ws) return;
  const [port, token] = connection.code.split(":");
  let socket;
  try { socket = new WebSocket(`ws://127.0.0.1:${port}/`); } catch (_) { lastError = "Invalid pairing code"; return; }
  ws = socket; lastError = "Connecting…";
  socket.onopen = () => socket.send(JSON.stringify({type:"hello", token}));
  socket.onmessage = event => {
    let data; try { data = JSON.parse(event.data); } catch (_) { return; }
    if (data.type === "ready" && ws === socket) {
      lastError = "Connected";
      if (lastState) socket.send(JSON.stringify(lastState));
      api.tabs.sendMessage(connection.tab, {type:"vod-report"}).catch(() => {});
    }
  };
  socket.onerror = () => { lastError = "Cannot reach the app"; };
  socket.onclose = () => {
    if (ws !== socket) return;
    ws = null; lastError = "Disconnected — retrying";
    if (connection) reconnectTimer = setTimeout(start, 3000);
  };
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  const popup = sender.url === api.runtime.getURL("popup.html");
  if (message.type === "vod-connect" && popup) {
    if (!validCode(message.code) || !Number.isInteger(message.tab) || message.tab < 0) {
      respond({ok:false, error:"Invalid pairing code or tab."}); return;
    }
    disconnect(false); connection = {code:message.code, tab:message.tab}; lastState = null;
    api.storage.local.set({vodConnection:connection}); start(); respond({ok:true}); return;
  }
  if (message.type === "vod-disconnect" && popup) { disconnect(true); respond({ok:true}); return; }
  if (message.type === "vod-status" && popup) { respond({status:lastError, tab:connection?.tab ?? null}); return; }
  if (message.type !== "vod-state" || !connection || sender.frameId !== 0 || sender.tab?.id !== connection.tab) return;
  // The URL is supplied by the browser, never by the page or an untrusted window message.
  const url = sender.url || sender.tab.url || "";
  const seconds = Number(message.seconds);
  if (!Number.isFinite(seconds) || seconds < 0 || seconds > 604800 || typeof message.paused !== "boolean") return;
  lastState = {type:"state", url, seconds, paused:message.paused, seek:message.seek === true};
  if (ws?.readyState === WebSocket.OPEN && lastError === "Connected") ws.send(JSON.stringify(lastState));
  if (lastState.seek) lastState = {...lastState, seek:false};
});
api.tabs.onRemoved.addListener(tab => { if (connection?.tab === tab) disconnect(true); });
api.storage.local.get("vodConnection").then(data => {
  if (!connection && data.vodConnection && validCode(data.vodConnection.code) && Number.isInteger(data.vodConnection.tab)) {
    connection = data.vodConnection; start();
  }
}).catch(() => {});
