const api = globalThis.browser || globalThis.chrome;
let lastVideo = null, seek = false, vodReporting = false, liveReporting = false, lastLiveUrl = "", lastLiveAt = 0;
function isVodPage() {
  const host = location.hostname.toLowerCase(), path = location.pathname;
  if (["www.youtube.com", "youtube.com", "m.youtube.com"].includes(host))
    return path === "/watch" && /^[A-Za-z0-9_-]{11}$/.test(new URL(location.href).searchParams.get("v") || "");
  if (["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(host)) return /^\/videos\/[0-9]+\/?$/.test(path);
  return ["kick.com", "www.kick.com"].includes(host) && /^\/[A-Za-z0-9_-]+\/videos\/[a-f0-9-]{36}\/?$/i.test(path);
}
function isLiveCandidate() {
  const host = location.hostname.toLowerCase(), path = location.pathname;
  if (["www.youtube.com", "youtube.com", "m.youtube.com"].includes(host))
    return (path === "/watch" && /^[A-Za-z0-9_-]{11}$/.test(new URL(location.href).searchParams.get("v") || "")) || /^\/live\/[A-Za-z0-9_-]{11}\/?$/.test(path);
  const twitch=["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(host);
  const kick=["kick.com", "www.kick.com"].includes(host);
  if (!twitch && !kick) return false;
  const match=path.match(twitch ? /^\/([A-Za-z0-9_]{1,25})\/?$/ : /^\/([A-Za-z0-9_-]{1,80})\/?$/);
  return !!match && !["directory","downloads","jobs","login","p","settings","signup","subscriptions","turbo","videos","wallet","categories","browse","search"].includes(match[1].toLowerCase());
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
function youtubeLiveHint() {
  if (!["www.youtube.com", "youtube.com", "m.youtube.com"].includes(location.hostname.toLowerCase())) return {};
  const flag = document.querySelector('meta[itemprop="isLiveBroadcast"]')?.content?.toLowerCase() === "true" ||
    !!document.querySelector('ytd-watch-flexy[is-live]');
  const owner = document.querySelector('ytd-watch-metadata #owner #channel-name a, ytd-video-owner-renderer #channel-name a, #owner ytd-channel-name a');
  const name = (owner?.textContent || "").replace(/\s+/g," ").trim().slice(0,100);
  return {live:flag && !!name, name};
}
function report() {
  const isVod = isVodPage(), isLive = isLiveCandidate();
  if (!isVod && vodReporting) { api.runtime.sendMessage({type:"vod-end"}).catch(() => {}); vodReporting=false; }
  if (!isLive && liveReporting) { api.runtime.sendMessage({type:"live-end"}).catch(() => {}); liveReporting=false; lastLiveUrl=""; }
  if (isLive && (location.href !== lastLiveUrl || Date.now()-lastLiveAt >= 5000)) {
    liveReporting=true;lastLiveUrl=location.href;lastLiveAt=Date.now();
    api.runtime.sendMessage({type:"live-state",url:location.href,...youtubeLiveHint()}).catch(() => {});
  }
  if (!isVod) return;
  const video = findVideo(); if (!video) return;
  if (lastVideo !== video) {
    if (lastVideo) lastVideo.removeEventListener("seeked", onSeek);
    lastVideo = video; video.addEventListener("seeked", onSeek); seek = true;
  }
  vodReporting = true;
  api.runtime.sendMessage({type:"vod-state", url:location.href, seconds:video.currentTime, paused:video.paused, seek}).catch(() => {});
  seek = false;
}
function onSeek() { seek = true; report(); }
setInterval(report, 500);
report();
