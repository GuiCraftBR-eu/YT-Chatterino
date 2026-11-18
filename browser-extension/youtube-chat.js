// Runs only in YouTube's live-chat frame. Read the poll banner, never the chat.
(() => {
const api = globalThis.browser || globalThis.chrome;
const clean = (value, limit=400) => (value || '').replace(/\s+/g, ' ').trim().slice(0, limit);
const shown = node => !!node && node.getClientRects().length > 0 && getComputedStyle(node).visibility !== 'hidden';

function videoId() {
  if (!['www.youtube.com','youtube.com','m.youtube.com'].includes(location.hostname.toLowerCase()) || location.pathname !== '/live_chat') return '';
  const id = new URL(location.href).searchParams.get('v') || '';
  return /^[A-Za-z0-9_-]{11}$/.test(id) ? id : '';
}
function snapshot() {
  const roots = [...document.querySelectorAll('yt-live-chat-poll-renderer')].filter(shown);
  if (roots.length !== 1) return null;
  const root = roots[0];
  const text = clean(root.querySelector('yt-live-chat-poll-header-renderer #poll-question')?.innerText);
  const nodes = [...root.querySelectorAll('yt-live-chat-poll-choice')].filter(shown);
  const choices = nodes.map((node, index) => ({id:'option-' + index,
    label:clean(node.querySelector('#label-text')?.innerText, 100)})).filter(choice => choice.label).slice(0,10);
  if (!text) return null;
  const selected = nodes.some(node => node.hasAttribute('selected'));
  const waiting = nodes.some(node => node.hasAttribute('vote-request-in-flight'));
  const results = nodes.some(node => node.hasAttribute('has-voted'));
  const open = choices.length === nodes.length && choices.length > 0 && !results && !waiting &&
    nodes.every(node => {
      const content = node.querySelector('#content');
      return shown(node.querySelector('#endpoint')) && !!content && !content.hasAttribute('disabled');
    });
  return {text,choices,state:selected && results && !waiting ? 'voted' : open ? 'open' : 'unavailable'};
}
async function vote(message) {
  const before = snapshot();
  if (!before) return {status:'ended'};
  if (before.text !== message.question) return {status:'changed'};
  if (before.state === 'voted') return {status:'already_voted'};
  if (before.state !== 'open' || !before.choices.some(choice => choice.id === message.choice && choice.label === message.label))
    return {status:'unavailable'};
  const roots = [...document.querySelectorAll('yt-live-chat-poll-renderer')].filter(shown);
  const root = roots[0];
  const index = Number(message.choice.slice('option-'.length));
  const nodes = [...root.querySelectorAll('yt-live-chat-poll-choice')].filter(shown);
  const node = nodes[index], endpoint = node?.querySelector('#endpoint');
  if (!shown(endpoint) || clean(node.querySelector('#label-text')?.innerText,100) !== message.label) return {status:'unavailable'};
  let sent = false, completed = false, failed = false;
  const onSent = event => {
    try { if (event.detail?.endpoint?.sendLiveChatVoteEndpoint) sent = true; } catch (_) { /* Unreadable event: remain uncertain. */ }
  };
  const onCompleted = () => { if (sent) completed = true; };
  const onError = () => { failed = true; };
  root.addEventListener('yt-service-request-sent', onSent);
  root.addEventListener('yt-service-request-completed', onCompleted);
  root.addEventListener('yt-service-request-error', onError);
  try {
    endpoint.click();
    for (let attempt=0; attempt<30; ++attempt) {
      await new Promise(resolve => setTimeout(resolve, 200));
      if (failed) return {status:'uncertain'};
      const after = snapshot();
      if (!after || after.text !== message.question) return {status:'uncertain'};
      const current = [...root.querySelectorAll('yt-live-chat-poll-choice')].filter(shown)[index];
      if (sent && completed && after.state === 'voted' && current?.hasAttribute('selected') &&
          clean(current.querySelector('#label-text')?.innerText,100) === message.label) return {status:'confirmed'};
    }
    return {status:'uncertain'};
  } finally {
    root.removeEventListener('yt-service-request-sent', onSent);
    root.removeEventListener('yt-service-request-completed', onCompleted);
    root.removeEventListener('yt-service-request-error', onError);
  }
}
api.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id !== api.runtime.id || message?.type !== 'vote-poll' || message.platform !== 'youtube' ||
      !videoId() || videoId() !== message.channel) return;
  vote(message).then(respond, () => respond({status:'uncertain'}));
  return true;
});
function report() {
  if (!videoId()) return;
  api.runtime.sendMessage({type:'youtube-chat-state',url:location.href,poll:snapshot() || {}}).catch(() => {});
}
setInterval(report, 2000);
report();
})();
