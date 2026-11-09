const api = globalThis.browser || globalThis.chrome;
const statusLabel = document.getElementById("status");
const pt = (navigator.language || "").toLowerCase().startsWith("pt");
const translations = {"Waiting for the app":"Aguardando o aplicativo", "Connecting…":"Conectando…",
  "Connected":"Conectado", "Waiting for a stream":"Aguardando uma transmissão"};
if (pt) {
  document.documentElement.lang = "pt-BR";
  document.getElementById("intro").textContent = "Abra uma live ou VOD compatível no navegador e selecione o chat pelo botão + no YT-Chatterino.";
  document.getElementById("privacy").textContent = "Endereços de canais e vídeos compatíveis e o estado da VOD são enviados apenas ao aplicativo neste computador.";
}
async function refresh() {
  const result = await api.runtime.sendMessage({type:"vod-status"});
  const state = result?.status || "Waiting for a stream";
  statusLabel.textContent = (pt ? (translations[state] || state) : state) + (result?.count ? ` · ${result.count}` : "");
}
refresh().catch(() => {}); setInterval(() => refresh().catch(() => {}), 1000);
