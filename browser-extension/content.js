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
  const match=path.match(twitch ? /^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/ : /^\/([A-Za-z0-9_-]{1,80})\/?$/);
  return !!match && !["directory","downloads","jobs","login","p","settings","signup","subscriptions","turbo","videos","wallet","categories","browse","search"].includes(match[1].toLowerCase());
}
// Read only Twitch's visible, dedicated banner elements. Never read the chat log,
// cookies, or account data. Missing chat UI is distinct from an empty banner.
function twitchEngagementCard(kind) {
  const pattern = kind === "poll" ? /\bpoll\b|enquete|vota[cç][aã]o/i :
    kind === "prediction" ? /predict|predi[cç][aã]o|previs[aã]o|aposta/i : null;
  if (!pattern) return null;
  return [...document.querySelectorAll('.community-highlight')].find(element => {
    const heading=element.querySelector('[data-test-selector="header-content"] [data-test-selector="header"]');
    return element.getClientRects().length > 0 && pattern.test((heading?.innerText || "").trim());
  }) || null;
}
function twitchChatPanels() {
  if (!["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) return undefined;
  const shown = node => !!node && node.getClientRects().length > 0 && getComputedStyle(node).visibility !== "hidden";
  if (!shown(document.querySelector('[data-test-selector="chat-room-component-layout"], .chat-room'))) return {available:false};
  const clean = (text, n=800) => (text || "").replace(/\s+/g," ").trim().slice(0,n);
  const textWithEmotes = root => {
    if (!root) return "";
    const walk = node => node.nodeType === Node.TEXT_NODE ? node.nodeValue :
      node.nodeType !== Node.ELEMENT_NODE ? "" : node.tagName === "IMG" ? (node.alt || "") :
      [...node.childNodes].map(walk).join("");
    return clean(walk(root));
  };
  const pinNode = document.querySelector('.pinned-chat__message, [data-test-selector="pinned-chat-message"] [data-a-target="chat-line-message-body"], [data-a-target="pinned-chat-message"] [data-a-target="chat-line-message-body"]');
  const pinScope = pinNode?.parentElement?.parentElement;
  const author = pinScope?.querySelector('.chatter-name, [data-a-target="chat-message-username"]');
  const pin = shown(pinNode) ? {text:textWithEmotes(pinNode),name:clean(author?.textContent,100)} : {};
  const panel = selector => {
    const node = document.querySelector(selector);
    const text = shown(node) ? clean(node.innerText,800) : "";
    return text ? {text} : {};
  };
  // Twitch currently renders polls as a generic community-highlight rather
  // than a poll-specific element. Only inspect the card above the chat log.
  const card = kind => {
    const root = twitchEngagementCard(kind);
    if (!root) return {};
    const heading=root.querySelector('[data-test-selector="header-content"] strong');
    const text=clean(heading?.innerText,400);
    const options=[...root.querySelectorAll('[data-test-selector="expanded-content"] button[role="progressbar"]')]
      .filter(shown).map(choice=>{
        const label=clean(choice.querySelector('[data-test-selector="title"]')?.innerText || choice.getAttribute('aria-label'),100);
        const count=clean(choice.querySelector('[data-test-selector="Count"]')?.innerText,60);
        return label + (count ? ' ' + count : '');
      }).filter(Boolean).slice(0,10).join(' · ');
    return text ? {text,options} : {};
  };
  const poll = card("poll");
  const prediction = card("prediction");
  return {available:true, pin,
    poll:poll.text ? poll : panel('[data-test-selector="poll-banner"], .poll-card'),
    prediction:prediction.text ? prediction : panel('[data-test-selector="community-prediction-banner"], .community-prediction-summary')};
}
// The desktop can request only that a matching Twitch panel be expanded.
// It cannot select an outcome, submit a vote, or spend channel points.
api.runtime.onMessage.addListener((message, sender) => {
  if (sender.id !== api.runtime.id) return;
  if (message?.type === "sync-now") { report(true); return; }
  if (message?.type !== "open-panel" || !isLiveCandidate()) return;
  if (!["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) return;
  const match=location.pathname.match(/^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/);
  if (!match || match[1].toLowerCase() !== message.channel || !["poll","prediction"].includes(message.kind)) return;
  const card=twitchEngagementCard(message.kind);
  if (card && !card.querySelector('[data-test-selector="expanded-content"]')) {
    card.querySelector('[data-test-selector="click-wrapper"]')?.click();
  }
});
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
function liveHint() {
  const youtube = youtubeLiveHint();
  if (youtube.live) return youtube;
  if (["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) {
    const match = location.pathname.match(/^\/([A-Za-z0-9_]{1,25})\/?$/);
    const indicator = document.querySelector('[data-a-target="video-ref"] .tw-channel-status-text-indicator');
    if (match && indicator && indicator.getClientRects().length && /^live$/i.test(indicator.textContent.trim())) {
      const title = document.title.replace(/\s*-\s*Twitch\s*$/i, '').trim();
      return {live:true, name:(title || match[1]).slice(0,100)};
    }
  }
  return youtube;
}
function report(force = false) {
  const isVod = isVodPage(), isLive = isLiveCandidate();
  if (!isVod && vodReporting) { api.runtime.sendMessage({type:"vod-end"}).catch(() => {}); vodReporting=false; }
  if (!isLive && liveReporting) { api.runtime.sendMessage({type:"live-end"}).catch(() => {}); liveReporting=false; lastLiveUrl=""; }
  if (isLive && (force || location.href !== lastLiveUrl || Date.now()-lastLiveAt >= 2000)) {
    liveReporting=true;lastLiveUrl=location.href;lastLiveAt=Date.now();
    api.runtime.sendMessage({type:"live-state",url:location.href,...liveHint(),chat:twitchChatPanels()}).catch(() => {});
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
