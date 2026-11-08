const api = globalThis.browser || globalThis.chrome;
let lastVideo = null, seek = false, reporting = false;
function isVodPage() {
  const host = location.hostname.toLowerCase(), path = location.pathname;
  if (["www.youtube.com", "youtube.com", "m.youtube.com"].includes(host))
    return path === "/watch" && /^[A-Za-z0-9_-]{11}$/.test(new URL(location.href).searchParams.get("v") || "");
  if (["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(host)) return /^\/videos\/[0-9]+\/?$/.test(path);
  return ["kick.com", "www.kick.com"].includes(host) && /^\/[A-Za-z0-9_-]+\/videos\/[a-f0-9-]{36}\/?$/i.test(path);
}
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
  if (!isVodPage()) {
    if (reporting) api.runtime.sendMessage({type:"vod-close"}).catch(() => {});
    reporting = false; return;
  }
  const video = findVideo();
  if (!video) return;
  if (lastVideo !== video) {
    if (lastVideo) lastVideo.removeEventListener("seeked", onSeek);
    lastVideo = video; video.addEventListener("seeked", onSeek); seek = true;
  }
  reporting = true;
  api.runtime.sendMessage({type:"vod-state", url:location.href, seconds:video.currentTime, paused:video.paused, seek}).catch(() => {});
  seek = false;
}
function onSeek() { seek = true; report(); }
setInterval(report, 500);
report();
