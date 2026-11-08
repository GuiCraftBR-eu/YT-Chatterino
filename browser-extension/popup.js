const api = globalThis.browser || globalThis.chrome;
const statusLabel = document.getElementById("status");
const pt = (navigator.language || "").toLowerCase().startsWith("pt");
const translations = {"Waiting for the app":"Aguardando o aplicativo", "Connecting…":"Conectando…",
  "Connected":"Conectado", "Waiting for a VOD":"Aguardando uma VOD"};
if (pt) {
  document.documentElement.lang = "pt-BR";
  document.getElementById("intro").textContent = "Abra o YT-Chatterino e uma VOD compatível. O chat aparece automaticamente no aplicativo.";
  document.getElementById("privacy").textContent = "Somente o endereço e a posição da VOD são enviados ao aplicativo no seu computador.";
}
async function refresh() {
  const result = await api.runtime.sendMessage({type:"vod-status"});
  const state = result?.status || "Waiting for a VOD";
  statusLabel.textContent = (pt ? (translations[state] || state) : state) + (result?.count ? ` · ${result.count} VOD` : "");
}
refresh().catch(() => {}); setInterval(() => refresh().catch(() => {}), 1000);
