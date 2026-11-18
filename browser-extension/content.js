const api = globalThis.browser || globalThis.chrome;
let lastVideo = null, seek = false, vodReporting = false, liveReporting = false, lastLiveUrl = "", lastLiveAt = 0;
const expandedPolls = new WeakSet();
let kickVotePending = "";
let kickRolePending = false;
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
    const heading=element.querySelector(kind === "prediction" ?
      '[data-test-selector="community-prediction-highlight-header__title"], [data-test-selector="header-content"] [data-test-selector="header"]' :
      '[data-test-selector="header-content"] [data-test-selector="header"]');
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
  const confirmation = [...(content?.querySelectorAll('p') || [])].some(node =>
    visiblePollNode(node) && !node.closest('button[role="progressbar"]') &&
    /^voted!?$/i.test(cleanPollText(node.innerText, 40)));
  const voted = confirmation || /(you(?:'ve| have)? voted|your vote (?:was|has been) (?:counted|recorded)|vote (?:recorded|submitted))\b|voc[eê] votou|seu voto (?:foi|est[aá]) (?:registrado|computado)/i.test(text);
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
// Kick's poll uses a radio group. Its options submit immediately and the UI is
// updated optimistically, so a checked button alone is not a confirmation.
function kickPollSnapshot() {
  const titles = [...document.querySelectorAll('[data-testid="poll-title"]')].filter(visiblePollNode);
  if (titles.length !== 1) return null;
  const title = titles[0];
  const root = title.parentElement?.parentElement?.parentElement;
  const lists = [...(root?.querySelectorAll('[data-testid="poll-option-list"]') || [])].filter(visiblePollNode);
  const text = cleanPollText(title.innerText);
  if (!text || lists.length !== 1) return {text, choices:[], state:"unavailable"};
  const buttons = [...lists[0].querySelectorAll('button[role="radio"]')].filter(visiblePollNode);
  const choices = buttons.map(button => ({id:button.getAttribute("id") || "",
    label:cleanPollText(button.getAttribute("title"), 100)}))
    .filter(choice => /^[0-9]{1,10}$/.test(choice.id) && choice.label).slice(0, 10);
  const voted = buttons.some(button => button.getAttribute("data-state") === "checked");
  const enabled = buttons.some(button => !button.disabled && button.getAttribute("aria-disabled") !== "true");
  return {text, choices, state:voted && kickVotePending !== text ? "voted" :
    !voted && enabled && choices.length === buttons.length && choices.length ? "open" : "unavailable"};
}
function kickChatPanels() {
  if (!["kick.com", "www.kick.com"].includes(location.hostname.toLowerCase())) return undefined;
  return {available:true, poll:kickPollSnapshot() || {}};
}
// The prediction checkout renders its question and outcomes in dedicated
// elements. An index is used only while the same checkout remains visible.
function twitchPredictionSnapshot() {
  const headers = [...document.querySelectorAll('.prediction-checkout-details-header')].filter(visiblePollNode);
  if (headers.length !== 1) return null;
  const text = cleanPollText(headers[0].firstElementChild?.innerText);
  const two = [...document.querySelectorAll('[data-test-selector="prediction-summary-outcome__title"]')].filter(visiblePollNode);
  const multi = [...document.querySelectorAll('[data-test-selector="multi-option-summary-outcome__title"]')].filter(visiblePollNode);
  const nodes = two.length === 2 && multi.length === 0 ? two : multi.length >= 3 && multi.length <= 10 && two.length === 0 ? multi : [];
  const choices = nodes.map((node, i) => ({id:"outcome-" + i, label:cleanPollText(node.innerText, 100)}));
  const toggle = document.querySelector('[data-test-selector="prediction-checkout-active-footer__input-type-toggle"]');
  if (!text) return null;
  return {text, choices, state:choices.length && choices.every(c => c.label) &&
    (multi.length >= 3 || visiblePollNode(toggle)) ? "open" : "unavailable"};
}
async function placeTwitchPrediction(message) {
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  const before = twitchPredictionSnapshot();
  if (!before || before.text !== message.question) return {status:"changed"};
  if (!Number.isInteger(message.points) || message.points < 1 || message.points > 250000 || before.state !== "open" ||
      !before.choices.some(c => c.id === message.choice && c.label === message.label)) return {status:"unavailable"};
  const index = Number(message.choice.slice('outcome-'.length));
  if (before.choices.length > 2) {
    const outcomes = [...document.querySelectorAll('[data-test-selector="multi-option-summary-outcome__title"]')].filter(visiblePollNode);
    if (!outcomes[index] || cleanPollText(outcomes[index].innerText,100) !== message.label) return {status:"changed"};
    outcomes[index].click();
    await wait(300);
    const details = [...document.querySelectorAll('[data-test-selector="multi-option-prediction-details__title"]')].filter(visiblePollNode);
    if (details.length !== 1 || cleanPollText(details[0].innerText,100) !== message.label) return {status:"changed"};
  }
  let fields = [...document.querySelectorAll('.custom-prediction-button')].filter(visiblePollNode);
  if (!fields.length) {
    const toggle = document.querySelector('[data-test-selector="prediction-checkout-active-footer__input-type-toggle"]');
    if (!visiblePollNode(toggle) || toggle.disabled) return {status:"unavailable"};
    toggle.click(); await wait(300);
    fields = [...document.querySelectorAll('.custom-prediction-button')].filter(visiblePollNode);
  }
  const headers = [...document.querySelectorAll('.prediction-checkout-details-header')].filter(visiblePollNode);
  if (headers.length !== 1 || cleanPollText(headers[0].firstElementChild?.innerText) !== message.question) return {status:"changed"};
  const multi = before.choices.length > 2;
  if ((multi && fields.length !== 1) || (!multi && fields.length !== 2)) return {status:"unavailable"};
  if (!multi) {
    const current = twitchPredictionSnapshot();
    if (!current || current.text !== message.question || current.choices.length !== 2 ||
        current.choices[index]?.label !== message.label) return {status:"changed"};
  } else {
    const details = [...document.querySelectorAll('[data-test-selector="multi-option-prediction-details__title"]')].filter(visiblePollNode);
    if (details.length !== 1 || cleanPollText(details[0].innerText,100) !== message.label) return {status:"changed"};
  }
  const field = fields[multi ? 0 : index];
  const input = field?.querySelector('input[type="number"]');
  if (!visiblePollNode(input) || input.disabled || input.value.trim()) return {status:"unavailable"};
  const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  if (!setter) return {status:"unavailable"};
  setter.call(input, String(message.points));
  input.dispatchEvent(new Event('input', {bubbles:true}));
  await wait(300);
  const currentChannel=location.pathname.match(/^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/);
  const currentHeaders=[...document.querySelectorAll('.prediction-checkout-details-header')].filter(visiblePollNode);
  const currentFields=[...document.querySelectorAll('.custom-prediction-button')].filter(visiblePollNode);
  if (!currentChannel || currentChannel[1].toLowerCase() !== message.channel || currentHeaders.length !== 1 ||
      cleanPollText(currentHeaders[0].firstElementChild?.innerText) !== message.question ||
      currentFields[multi ? 0 : index] !== field) return {status:"changed"};
  if (multi) {
    const details=[...document.querySelectorAll('[data-test-selector="multi-option-prediction-details__title"]')].filter(visiblePollNode);
    if (details.length !== 1 || cleanPollText(details[0].innerText,100) !== message.label) return {status:"changed"};
  } else {
    const current=twitchPredictionSnapshot();
    if (!current || current.choices[index]?.label !== message.label) return {status:"changed"};
  }
  const submit = field.querySelector('button');
  if (input.value !== String(message.points) || !visiblePollNode(submit) || submit.disabled ||
      submit.getAttribute('aria-disabled') === 'true' || !/^(vote|votar)$/i.test(cleanPollText(submit.innerText,40)))
    return {status:"unavailable"};
  submit.click();
  for (let attempt=0; attempt<25; ++attempt) {
    await wait(200);
    if ([...document.querySelectorAll('[data-test-selector="prediction-terms-step__accept-button"], [data-test-selector="prediction-mod-confirmation__accept-button"]')].some(visiblePollNode))
      return {status:"needs_confirmation"};
  }
  // The checkout is optimistic and has several possible completion states.
  // Avoid claiming a points wager succeeded without an authoritative result.
  return {status:"uncertain"};
}
async function voteOnKickPoll(message) {
  const snapshot = kickPollSnapshot();
  if (!snapshot) return {status:"ended"};
  if (snapshot.text !== message.question) return {status:"changed"};
  if (snapshot.state === "voted") return {status:"already_voted"};
  if (snapshot.state !== "open" || !snapshot.choices.some(choice => choice.id === message.choice && choice.label === message.label))
    return {status:"unavailable"};
  const titles = [...document.querySelectorAll('[data-testid="poll-title"]')].filter(visiblePollNode);
  const root = titles[0]?.parentElement?.parentElement?.parentElement;
  const option = [...(root?.querySelectorAll('[data-testid="poll-option-list"] button[role="radio"]') || [])]
    .find(button => visiblePollNode(button) && button.getAttribute("id") === message.choice &&
      cleanPollText(button.getAttribute("title"), 100) === message.label);
  if (!option || option.disabled || option.getAttribute("aria-disabled") === "true") return {status:"unavailable"};
  kickVotePending = message.question;
  try {
    option.click();
    // Ask the same-origin endpoint for the server's state; this uses the browser's
    // existing session without exposing any credentials to the app.
    const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
    for (let attempt = 0; attempt < 6; ++attempt) {
      await wait(400);
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 900);
      try {
        const response = await fetch('/api/v2/channels/' + encodeURIComponent(message.channel) + '/polls',
          {credentials:"same-origin", cache:"no-store", signal:controller.signal});
        if (!response.ok) continue;
        const poll = (await response.json())?.data?.poll;
        if (cleanPollText(poll?.title) !== message.question) return {status:"uncertain"};
        if (poll?.has_voted === true && String(poll.voted_option_id) === message.choice &&
            poll.options?.some(choice => String(choice.id) === message.choice && cleanPollText(choice.label, 100) === message.label))
          return {status:"confirmed"};
      } catch (_) { /* The server did not confirm; do not submit again. */ }
      finally { clearTimeout(timeout); }
    }
    return {status:"uncertain"};
  } finally {
    kickVotePending = "";
  }
}
// Kick handles these commands in its own chat editor. Never overwrite a draft
// and never report success solely because the send button was clicked.
async function changeKickRole(message) {
  const verbs = ["mod", "unmod", "vip", "unvip", "og", "unog"];
  if (kickRolePending || !verbs.includes(message.verb) || !/^[A-Za-z0-9_]{1,25}$/.test(message.username) ||
      !/^[0-9]{1,20}$/.test(message.uid))
    return {status:"unavailable"};
  const editors = [...document.querySelectorAll('[data-testid="chat-input"][contenteditable="true"]')].filter(visiblePollNode);
  const buttons = [...document.querySelectorAll('#send-message-button')].filter(visiblePollNode);
  const hasDraft = editor => !!cleanPollText(editor.innerText,500) ||
    !!editor.querySelector('img, svg, video, [data-lexical-decorator], [contenteditable="false"]');
  if (editors.length !== 1 || buttons.length !== 1 || hasDraft(editors[0])) return {status:"unavailable"};
  const editor=editors[0], command='/' + message.verb + ' ' + message.username;
  let submitted=false;
  kickRolePending=true;
  try {
    const identity = async () => {
      const controller=new AbortController();
      const timeout=setTimeout(()=>controller.abort(),900);
      try {
        const response=await fetch('/api/v2/channels/' + encodeURIComponent(message.channel) + '/users/' + encodeURIComponent(message.username),
          {credentials:"same-origin",cache:"no-store",signal:controller.signal});
        return response.ok ? await response.json() : null;
      } catch (_) { return null; } finally { clearTimeout(timeout); }
    };
    const before=await identity();
    if (!before) return {status:"unavailable"};
    if (String(before.id) !== message.uid || cleanPollText(before.username,100).toLowerCase() !== message.username.toLowerCase())
      return {status:"changed"};
    const moderator=["mod","unmod"].includes(message.verb), desired=message.verb === "mod";
    if (moderator && typeof before.is_moderator === "boolean" && before.is_moderator === desired) return {status:"already_set"};
    if (hasDraft(editor)) return {status:"unavailable"};
    editor.focus();
    if (document.activeElement !== editor) return {status:"unavailable"};
    const selection=window.getSelection(), range=document.createRange();
    if (!selection) return {status:"unavailable"};
    range.selectNodeContents(editor.querySelector('p:last-child') || editor); range.collapse(true);
    selection.removeAllRanges(); selection.addRange(range);
    await new Promise(resolve => setTimeout(resolve,100));
    editor.dispatchEvent(new InputEvent('beforeinput', {bubbles:true, cancelable:true, inputType:'insertText', data:command}));
    await new Promise(resolve => setTimeout(resolve,100));
    if (!cleanPollText(editor.innerText,500) && !document.execCommand('insertText',false,command)) return {status:"unavailable"};
    await new Promise(resolve => setTimeout(resolve,300));
    const match=location.pathname.match(/^\/([A-Za-z0-9_-]{1,80})\/?$/);
    if (!match || match[1].toLowerCase() !== message.channel || cleanPollText(editor.innerText,500) !== command ||
        !visiblePollNode(buttons[0]) || buttons[0].disabled || buttons[0].getAttribute('aria-disabled') === 'true')
      return {status:"unavailable"};
    buttons[0].click(); submitted=true;
    if (moderator) for (let attempt=0; attempt<6; ++attempt) {
      await new Promise(resolve => setTimeout(resolve,400));
      const current=await identity();
      if (current && String(current.id) === message.uid && current.is_moderator === desired) return {status:"confirmed"};
    }
    return {status:"uncertain"};
  } finally {
    if (!submitted && document.activeElement === editor && cleanPollText(editor.innerText,500) === command) {
      document.execCommand('selectAll',false,null);
      document.execCommand('delete',false,null);
    }
    kickRolePending=false;
  }
}
function twitchChatPanels() {
  if (!["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) return undefined;
  const shown = node => !!node && node.getClientRects().length > 0 && getComputedStyle(node).visibility !== "hidden";
  const chatRoot = document.querySelector('[data-test-selector="chat-room-component-layout"], .chat-room');
  if (!shown(chatRoot)) return {available:false};
  const clean = (text, n=800) => (text || "").replace(/\s+/g," ").trim().slice(0,n);
  const textWithEmotes = root => {
    if (!root) return {text:"",native_emotes:[]};
    let raw=""; const found=[];
    const walk = node => {
      if (node.nodeType === Node.TEXT_NODE) { raw += node.nodeValue || ""; return; }
      if (node.nodeType !== Node.ELEMENT_NODE) return;
      if (node.tagName === "IMG") {
        const code=clean(node.alt,100), start=raw.length; raw+=code;
        if (code && found.length < 12) {
          try {
            const url=new URL(node.currentSrc || node.src || "",location.href);
            if (url.protocol === "https:" && !url.username && !url.password && (!url.port || url.port === "443") && url.href.length <= 240)
              found.push({start,code,url:url.href});
          } catch (_) {}
        }
        return;
      }
      for (const child of node.childNodes) walk(child);
    };
    walk(root);
    const text=clean(raw), native_emotes=[];
    for (const emote of found) {
      const start=raw.slice(0,emote.start).replace(/\s+/g," ").replace(/^\s+/,"").length;
      const end=start+emote.code.length;
      if (end <= text.length && text.slice(start,end) === emote.code)
        native_emotes.push({start,end,code:emote.code,urls:[emote.url]});
    }
    return {text,native_emotes};
  };
  const pinNode = document.querySelector('.pinned-chat__message, [data-test-selector="pinned-chat-message"] [data-a-target="chat-line-message-body"], [data-a-target="pinned-chat-message"] [data-a-target="chat-line-message-body"]');
  const pinScope = pinNode?.parentElement?.parentElement;
  const author = pinScope?.querySelector('.chatter-name, [data-a-target="chat-message-username"]');
  const pin = shown(pinNode) ? {...textWithEmotes(pinNode),name:clean(author?.textContent,100)} : {};
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
    const text=clean(heading?.innerText || (kind === "prediction" ?
      root.querySelector('[data-test-selector="header-content"]')?.innerText : ""),400);
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
  const prediction = twitchPredictionSnapshot() || card("prediction");
  // The current Twitch banner uses a generated class prefix and an accessible
  // progress bar. Limit inspection to the channel's visible chat, not the page.
  const banners=[...chatRoot.querySelectorAll('[class*="hypeTrainBanner--"], [class*="bannerButton--"][aria-controls], [data-test-selector="hype-train-banner"]')].filter(shown);
  const banner=banners.find(node => node.matches?.('[class*="bannerButton--"], [data-test-selector="hype-train-banner"]')) || banners[0];
  const progress=banner?.querySelector('[role="progressbar"]');
  const timer=banner?.querySelector('[role="timer"]');
  const progressNow=Number(progress?.getAttribute('aria-valuenow'));
  const progressMax=Number(progress?.getAttribute('aria-valuemax'));
  const parts=[];
  if (progress && Number.isFinite(progressNow) && Number.isFinite(progressMax) && progressMax > 0 && progressNow >= 0)
    parts.push(`${progressNow}/${progressMax}`);
  const time=clean(timer?.innerText,100); if (time) parts.push(time);
  const hype=banner ? {text:clean(banner.getAttribute('aria-label') || banner.innerText || 'Hype Train',256),options:parts.join(' · ')} : {};
  return {available:true, pin, hype,
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
  if (!["open-panel", "vote-poll", "place-prediction", "kick-role"].includes(message?.type) || !isLiveCandidate()) return;
  if (["kick.com", "www.kick.com"].includes(location.hostname.toLowerCase())) {
    const match = location.pathname.match(/^\/([A-Za-z0-9_-]{1,80})\/?$/);
    if (!["vote-poll", "kick-role"].includes(message.type) || !match || match[1].toLowerCase() !== message.channel || message.platform !== "kick") return;
    if (message.type === "kick-role") {
      changeKickRole(message).then(respond, () => respond({status:"uncertain"}));
      return true;
    }
    voteOnKickPoll(message).then(respond, () => respond({status:"uncertain"}));
    return true;
  }
  if (!["www.twitch.tv", "twitch.tv", "m.twitch.tv"].includes(location.hostname.toLowerCase())) return;
  const match=location.pathname.match(/^\/(?:popout\/)?([A-Za-z0-9_]{1,25})(?:\/chat)?\/?$/);
  if (!match || match[1].toLowerCase() !== message.channel) return;
  if (message.type === "place-prediction") {
    placeTwitchPrediction(message).then(respond, () => respond({status:"uncertain"}));
    return true;
  }
  if (message.type === "vote-poll") {
    voteOnTwitchPoll(message).then(respond, () => respond({status:"unavailable"}));
    return true;
  }
  if (message.type !== "open-panel" || !["poll","prediction"].includes(message.kind)) return;
  const card=twitchEngagementCard(message.kind);
  if (message.kind === "prediction" && card && !twitchPredictionSnapshot()) {
    const button=[...card.querySelectorAll('button')].find(node => visiblePollNode(node) &&
      /^(predict|prever|fazer previs[aã]o)$/i.test(cleanPollText(node.innerText,40)));
    if (button && !button.disabled) { button.click(); return; }
  }
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
    api.runtime.sendMessage({type:"live-state",url:location.href,...liveHint(),chat:twitchChatPanels() || kickChatPanels()}).catch(() => {});
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
