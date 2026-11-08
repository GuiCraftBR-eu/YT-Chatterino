const api = globalThis.browser || globalThis.chrome;
const status = document.getElementById("status"), code = document.getElementById("code");
const pt = (navigator.language || "").toLowerCase().startsWith("pt");
const translations = {
  "Not connected":"Não conectado", "Connecting…":"Conectando…", "Connected":"Conectado",
  "Cannot reach the app":"Não foi possível conectar ao aplicativo", "Disconnected — retrying":"Desconectado — tentando novamente",
  "Invalid pairing code":"Código de pareamento inválido", "Invalid pairing code or tab.":"Código de pareamento ou aba inválidos.",
  "Open a supported VOD in this tab first.":"Abra uma VOD compatível nesta aba primeiro.", "Could not connect":"Não foi possível conectar"
};
const tr = text => pt ? (translations[text] || text) : text;
if (pt) {
  document.documentElement.lang = "pt-BR";
  document.getElementById("intro").textContent = "Abra uma VOD da Twitch, do YouTube ou da Kick nesta aba. Cole o código exibido no YT-Chatterino.";
  document.getElementById("code-label").textContent = "Código de pareamento";
  code.placeholder = "Porta:código";
  document.getElementById("connect").textContent = "Conectar esta aba";
  document.getElementById("disconnect").textContent = "Desconectar";
  document.getElementById("privacy").textContent = "Somente o endereço e a posição do vídeo desta aba são enviados ao aplicativo no seu computador.";
}
async function refresh() {
  const result = await api.runtime.sendMessage({type:"vod-status"});
  status.textContent = tr(result?.status || "Not connected");
}
document.getElementById("connect").addEventListener("click", async () => {
  const [tab] = await api.tabs.query({active:true, currentWindow:true});
  const url = tab?.url || "";
  if (!/^https:\/\/(?:www\.|m\.)?(?:youtube\.com|twitch\.tv|kick\.com)\//i.test(url)) {
    status.textContent = tr("Open a supported VOD in this tab first."); return;
  }
  const result = await api.runtime.sendMessage({type:"vod-connect", code:code.value.trim(), tab:tab.id});
  if (!result?.ok) status.textContent = tr(result?.error || "Could not connect");
  else { code.value = ""; await refresh(); }
});
document.getElementById("disconnect").addEventListener("click", async () => { await api.runtime.sendMessage({type:"vod-disconnect"}); refresh(); });
refresh(); setInterval(refresh, 1000);
