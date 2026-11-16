const api = globalThis.browser || globalThis.chrome;
let lastVideo = null, seek = false, vodReporting = false, liveReporting = false, lastLiveUrl = "", lastLiveAt = 0;
const expandedPolls = new WeakSet();
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
const cleanPollText = (text, length=400) => (text || "").replace(/\s+/g, " ").trim().slice(0, length);
const visiblePollNode = node => !!node && node.getClientRects().length > 0 && getComputedStyle(node).visibility !== "hidden";
function pollSnapshot(root) {
  if (!root || !visiblePollNode(root)) return null;
  const title = cleanPollText(root.querySelector('[data-test-selector="header-content"] strong')?.innerText);
  const header = cleanPollText(root.querySelector('[data-test-selector="header-content"] [data-test-selector="header"]')?.innerText);
  const content = root.querySelector('[data-test-selector="expanded-content"]');
  const text = cleanPollText(content?.innerText, 1200);
  const ended = /\b(poll (ended|results?|closed)|final results?)\b|enquete encerrada|resultado da enquete/i.test(header + " " + text);
  const voted = /(you(?:'ve| have)? voted|your vote (?:was|has been) (?:counted|recorded)|vote (?:recorded|submitted))\b|voc[eê] votou|seu voto (?:foi|est[aá]) (?:registrado|computado)/i.test(text);
  const choices = [...(content?.querySelectorAll('button[role="progressbar"]') || [])].filter(visiblePollNode).map(node => {
    const input = node.querySelector('input[type="radio"]');
    return {id:cleanPollText(input?.value || input?.id, 128),
      label:cleanPollText(node.querySelector('[data-test-selector="title"]')?.innerText || node.getAttribute('aria-label'), 100),
      count:cleanPollText(node.querySelector('[data-test-selector="Count"]')?.innerText, 60)};
  }).filter(choice => choice.id && choice.label).slice(0, 10);
  const voteButton = [...(content?.querySelectorAll('button') || [])].find(node =>
    visiblePollNode(node) && /^(vote|votar)$/i.test(cleanPollText(node.getAttribute('aria-label') || node.innerText, 40)));
  return {text:title, choices, state:ended ? "ended" : voted ? "voted" : (voteButton && choices.length ? "open" : "unavailable")};
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
    if (kind === "poll" && !root.querySelector('[data-test-selector="expanded-content"]') && !expandedPolls.has(root)) {
      expandedPolls.add(root);
      root.querySelector('[data-test-selector="click-wrapper"]')?.click();
    }
    const heading=root.querySelector('[data-test-selector="header-content"] strong');
    const text=clean(heading?.innerText,400);
    const options=[...root.querySelectorAll('[data-test-selector="expanded-content"] button[role="progressbar"]')]
      .filter(shown).map(choice=>{
        const label=clean(choice.querySelector('[data-test-selector="title"]')?.innerText || choice.getAttribute('aria-label'),100);
        const count=clean(choice.querySelector('[data-test-selector="Count"]')?.innerText,60);
        return label + (count ? ' ' + count : '');
      }).filter(Boolean).slice(0,10).join(' · ');
    const poll = kind === "poll" ? pollSnapshot(root) : null;
    return text ? {text,options,...(poll ? {choices:poll.choices,state:poll.state} : {})} : {};
  };
  const poll = card("poll");
  const prediction = card("prediction");
  return {available:true, pin,
    poll:poll.text ? poll : panel('[data-test-selector="poll-banner"], .poll-card'),
    prediction:prediction.text ? prediction : panel('[data-test-selector="community-prediction-banner"], .community-prediction-summary')};
}
// Only a free poll vote is supported. Predictions and paid votes remain on Twitch.
async function voteOnTwitchPoll(message) {
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  let root = twitchEngagementCard("poll");
  if (!root) return {status:"ended"};
  if (!root.querySelector('[data-test-selector="expanded-content"]')) {
    root.querySelector('[data-test-selector="click-wrapper"]')?.click();
    await wait(300);
  }
  let snapshot = pollSnapshot(root);
  if (!snapshot || snapshot.text !== message.question) return {status:"changed"};
  if (snapshot.state === "ended") return {status:"ended"};
  if (snapshot.state === "voted") return {status:"already_voted"};
  const match = snapshot.choices.find(choice => choice.id === message.choice && choice.label === message.label);
  if (!match || snapshot.state !== "open") return {status:"unavailable"};
  const content = root.querySelector('[data-test-selector="expanded-content"]');
  const option = [...content.querySelectorAll('button[role="progressbar"]')].find(node => {
    const input = node.querySelector('input[type="radio"]');
    return visiblePollNode(node) && (input?.value || input?.id) === match.id &&
      cleanPollText(node.querySelector('[data-test-selector="title"]')?.innerText || node.getAttribute('aria-label'), 100) === match.label;
  });
  if (!option || option.disabled) return {status:"unavailable"};
  option.click();
  await wait(200);
  root = twitchEngagementCard("poll");
  if (!root) return {status:"changed"};
  snapshot = pollSnapshot(root);
  if (!snapshot || snapshot.text !== message.question || snapshot.state === "ended") return {status:"ended"};
  if (snapshot.state === "voted") return {status:"already_voted"};
  const current = root.querySelector('[data-test-selector="expanded-content"]');
  const selected = [...(current?.querySelectorAll('input[type="radio"]') || [])].some(input =>
    (input.value || input.id) === match.id && input.checked);
  const vote = [...(current?.querySelectorAll('button') || [])].find(node => visiblePollNode(node) &&
    /^(vote|votar)$/i.test(cleanPollText(node.getAttribute('aria-label') || node.innerText, 40)));
  if (!selected || !vote || vote.disabled || vote.getAttribute('aria-disabled') === 'true') return {status:"unavailable"};
  if (/\b(points?|pontos?|bits?)\b|\d/i.test(cleanPollText(vote.innerText + ' ' + vote.getAttribute('title'), 200)))
    return {status:"unavailable"};
  vote.click();
  for (let attempt=0; attempt<30; ++attempt) {
    await wait(200);
    const updated = twitchEngagementCard("poll");
    if (!updated) return {status:"uncertain"};
    const result = pollSnapshot(updated);
    if (!result || result.text !== message.question) return {status:"uncertain"};
    if (result.state === "voted") return {status:"confirmed"};
    if (result.state === "ended") return {status:"uncertain"};
  }
  return {status:"uncertain"};
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id !== api.runtime.id) return;
  if (message?.type === "sync-now") { report(true); return; }
  if (!["open-panel", "vote-poll"].includes(message?.type) || !isLiveCandidate()) return;
  if (!["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) return;
  const match=location.pathname.match(/^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/);
  if (!match || match[1].toLowerCase() !== message.channel) return;
  if (message.type === "vote-poll") {
    voteOnTwitchPoll(message).then(respond, () => respond({status:"unavailable"}));
    return true;
  }
  if (message.type !== "open-panel" || !["poll","prediction"].includes(message.kind)) return;
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
