const api = globalThis.browser || globalThis.chrome;
let lastVideo = null, seek = false;
function findVideo() {
  const candidates = [...document.querySelectorAll("video")].filter(video => {
    const box = video.getBoundingClientRect();
    return Number.isFinite(video.currentTime) && box.width > 30 && box.height > 30;
  });
  candidates.sort((a, b) => {
    const x = a.getBoundingClientRect(), y = b.getBoundingClientRect();
    return y.width * y.height - x.width * x.height;
  });
  return candidates[0] || null;
}
function report() {
  const video = findVideo();
  if (!video) return;
  if (lastVideo !== video) {
    if (lastVideo) lastVideo.removeEventListener("seeked", onSeek);
    lastVideo = video; video.addEventListener("seeked", onSeek); seek = true;
  }
  api.runtime.sendMessage({type:"vod-state", seconds:video.currentTime, paused:video.paused, seek}).catch(() => {});
  seek = false;
}
function onSeek() { seek = true; report(); }
api.runtime.onMessage.addListener(message => { if (message.type === "vod-report") report(); });
setInterval(report, 500);
report();
