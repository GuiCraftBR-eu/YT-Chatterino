const api = globalThis.browser || globalThis.chrome;
const statusLabel = document.getElementById("status");
const pt = (navigator.language || "").toLowerCase().startsWith("pt");
const translations = {"Waiting for the app":"Aguardando o aplicativo", "Connecting…":"Conectando…",
  "Connected":"Conectado", "Waiting for a VOD":"Aguardando uma VOD"};
if (pt) {
  document.documentElement.lang = "pt-BR";
  document.getElementById("intro").textContent = "Abra uma VOD compatível no navegador e selecione o chat pelo botão + no YT-Chatterino.";
  document.getElementById("privacy").textContent = "Os endereços e o estado de reprodução são enviados apenas ao aplicativo neste computador. Páginas de vídeo do YouTube são verificadas para identificar replays.";
}
async function refresh() {
  const result = await api.runtime.sendMessage({type:"vod-status"});
  const state = result?.status || "Waiting for a VOD";
  statusLabel.textContent = (pt ? (translations[state] || state) : state) + (result?.count ? ` · ${result.count} VOD` : "");
}
refresh().catch(() => {}); setInterval(() => refresh().catch(() => {}), 1000);
