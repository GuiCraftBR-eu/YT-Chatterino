'use strict';
const $ = id => document.getElementById(id);
let cursor = 0, revision = 0, sources = [], replySeq = 0, polling = false, timer = null, catalog = [];
let detail = null, detailTimer = null, detailRequest = 0;
let stripeBySeq = new Map(), nextStripe = false, categoryTimer = null, categoryRequest = 0;
let language = 'en';
const pt = {
  'Enter the six-digit code shown on your computer.':'Digite o código de seis dígitos mostrado no computador.',
  'Pairing code':'Código de pareamento', 'Connect':'Conectar',
  'Use a trusted local network. The computer must remain on with the app open.':'Use uma rede local confiável. O computador deve permanecer ligado com o app aberto.',
  'Disconnect':'Desconectar', 'Send':'Enviar', 'Connection failed.':'Falha na conexão.',
  'Please wait and try again.':'Aguarde e tente novamente.', 'Reply':'Responder',
  'Replying to ':'Respondendo a ', 'Moderate':'Moderar',
  'Delete message':'Excluir mensagem', 'Timeout · 10 minutes':'Timeout · 10 minutos',
  'Ban user':'Banir usuário', 'Unban user':'Desbanir usuário', 'Cancel':'Cancelar',
  'Confirm action for ':'Confirmar ação para ',
  'No open channels':'Nenhum canal aberto', 'Disconnected. Retrying…':'Desconectado. Tentando novamente…',
  'Disconnected.':'Desconectado.', 'Send a message…':'Envie uma mensagem…',
  'Invalid or expired code.':'Código inválido ou expirado.',
  'Pair again on your PC.':'Faça o pareamento novamente no computador.',
  'Invalid channel.':'Canal inválido.', 'Invalid message or reply.':'Mensagem ou resposta inválida.',
  'Moderation is unavailable.':'Moderação indisponível.', 'Please wait.':'Aguarde.',
  'Search emotes':'Buscar emotes', 'Channel':'Canal', 'Message':'Mensagem', 'Choose emote':'Escolher emote',
  'PHONE':'CELULAR', 'Reply thread':'Conversa', 'Message history':'Histórico de mensagens',
  'Only messages retained in this app are available.':'Somente mensagens mantidas neste aplicativo estão disponíveis.',
  'Message is no longer available.':'A mensagem não está mais disponível.', 'No reply thread.':'Não há conversa de resposta.',
  'Copy':'Copiar', 'Copied!':'Copiado!', 'Select the name to copy it.':'Selecione o nome para copiá-lo.',
  'Edit channel':'Editar canal', 'Stream title':'Título da live', 'Search category':'Buscar categoria',
  'Keep current category':'Manter categoria atual', 'Save':'Salvar', 'Choose a category':'Escolha uma categoria',
  'Channel editing is unavailable.':'Edição do canal indisponível.',
  'Enable the required permissions on the computer.':'Ative as permissões necessárias no computador.',
  'Invalid mention.':'Menção inválida.', 'No retained messages.':'Nenhuma mensagem armazenada.'
};
function t(text) { return language === 'pt-BR' ? (pt[text] || text) : text; }
function translate() {
  document.documentElement.lang = language;
  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = t(el.dataset.i18n); });
  $('message').placeholder = t('Send a message…'); $('emote-search').placeholder = t('Search emotes');
  $('channel').setAttribute('aria-label', t('Channel')); $('message').setAttribute('aria-label', t('Message'));
  $('show-emotes').setAttribute('aria-label', t('Choose emote'));
}
async function request(path, options = {}) {
  const response = await fetch(path, {credentials:'same-origin', cache:'no-store', ...options});
  let data = {};
  try { data = await response.json(); } catch (_) {}
  if (!response.ok) { const error = new Error(t(data.error || (response.status === 429 ? 'Please wait and try again.' : 'Connection failed.'))); error.status = response.status; throw error; }
  return data;
}
function pairView(message = '') {
  clearTimeout(timer); closeDetail();
  for (const id of ['preview-dialog', 'edit-dialog', 'mod-dialog']) if ($(id).open) $(id).close();
  $('chat').hidden = true; $('pairing').hidden = false; $('pair-error').textContent = message;
}
async function pair(code) {
  $('pair-error').textContent = '';
  try {
    await request('/api/pair', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({code})});
    cursor = 0; revision = 0; stripeBySeq.clear(); nextStripe = false; $('messages').replaceChildren(); $('pairing').hidden = true; $('chat').hidden = false; poll();
  } catch (error) { pairView(error.message); }
}
$('pair-form').addEventListener('submit', event => { event.preventDefault(); pair($('code').value); });
function node(tag, text, cls) { const el = document.createElement(tag); if (text !== undefined) el.textContent = text; if (cls) el.className = cls; return el; }
function content(parent, message, offset) {
  let position = offset;
  const pieces = [...(message.emotes || []).map(span => ({...span, kind:'emote'})),
                  ...(message.mentions || []).map((span, index) => ({...span, kind:'mention', index}))]
                 .sort((a,b) => a.start - b.start || (a.kind === 'emote' ? -1 : 1));
  for (const span of pieces) {
    if (span.start < position || span.end <= span.start || span.end > message.text.length) continue;
    parent.append(document.createTextNode(message.text.slice(position, span.start)));
    if (span.kind === 'emote') {
      const img = node('img', undefined, 'emote previewable'); img.src = span.url; img.alt = span.name; img.title = span.name;
      img.loading = 'lazy'; img.tabIndex = 0; img.dataset.preview = 'emote'; parent.append(img);
    } else if (message.seq) {
      const mention = node('button', message.text.slice(span.start, span.end), 'mention-link'); mention.type = 'button';
      mention.addEventListener('click', () => showDetail('user-history', message, span.index)); parent.append(mention);
    } else parent.append(document.createTextNode(message.text.slice(span.start, span.end)));
    position = span.end;
  }
  parent.append(document.createTextNode(message.text.slice(position)));
}
function selectChannel(key) {
  const channel = $('channel'); if ([...channel.options].some(o => o.value === key)) channel.value = key;
  updateEditButton();
}
function updateEditButton() {
  const source = sources.find(s => s.key === $('channel').value);
  $('edit-channel').hidden = !source || !source.editable;
}
function channelIcon(source) {
  const icon = source.avatar ? node('img', undefined, 'channel-icon') : node('span', source.name.charAt(0).toUpperCase(), 'channel-fallback');
  if (source.avatar) {
    icon.src = source.avatar; icon.alt = source.name; icon.loading = 'lazy';
    icon.addEventListener('error', () => {
      const fallback = channelIcon({...source, avatar:''}); icon.replaceWith(fallback);
    }, {once:true});
  }
  icon.title = source.name; icon.setAttribute('aria-label', source.name); icon.dataset.channelIcon = source.key;
  return icon;
}
function messageRow(m, inThread = false, stripe = false) {
  const source = sources.find(s => s.key === m.source);
  const row = node('div', undefined, 'row' + (stripe ? ' stripe' : '') + (m.special ? ' alert' : '') + (source ? ' ' + source.platform : '') + (m.deleted ? ' deleted' : ''));
  row.dataset.seq = String(m.seq || 0);
  if (m.system) { row.append(node('span', m.time + ' ' + m.text, 'system')); return row; }
  if (!inThread && m.seq && (m.reply?.name || m.reply?.login || m.reply?.text)) {
    const preview = node('button', '↪ ' + t('Replying to ') + '@' + (m.reply.name || m.reply.login || '?') + ': ' + (m.reply.text || ''), 'reply-preview');
    preview.type = 'button'; preview.addEventListener('click', () => showDetail('thread', m)); row.append(preview);
  }
  if (m.special) {
    const event = node('div', undefined, 'event'); const eventName = node('div', undefined, 'event-name');
    const crown = node('img', undefined, 'event-icon'); crown.src = '/prime-crown.png'; crown.alt = '';
    const alertName = node(m.seq ? 'button' : 'span', m.name, m.seq ? 'user-link' : '');
    if (m.seq) { alertName.type = 'button'; alertName.addEventListener('click', () => showDetail('user-history', m)); }
    eventName.append(crown, alertName); event.append(eventName);
    event.append(node('div', m.alert || m.text, 'event-desc')); row.append(event);
  }
  const offset = m.special && m.alert ? Math.max(0, Math.min(m.alertBodyStart || 0, m.text.length)) : (m.special ? m.text.length : 0);
  if (offset < m.text.length) {
    const body = node('div', undefined, 'body');
    if (sources.length > 1 && source) body.append(channelIcon(source));
    body.append(node('span', m.time + ' ', 'time'));
    for (const badge of m.badges || []) if (badge.url) { const img = node('img', undefined, 'badge previewable'); img.src = badge.url; img.alt = badge.name || 'badge'; img.title = badge.name || 'badge'; img.loading = 'lazy'; img.tabIndex = 0; img.dataset.preview = 'badge'; body.append(img); }
    const name = node(m.seq ? 'button' : 'span', m.name + ': ', m.seq ? 'name user-link' : 'name');
    name.style.color = m.color || '#efeff1';
    if (m.seq) { name.type = 'button'; name.addEventListener('click', () => showDetail('user-history', m)); }
    body.append(name); content(body, m, offset);
    if (m.seq && source && (source.platform === 'twitch' || source.platform === 'kick') && !m.deleted) {
      const reply = node('button', '↩', 'action'); reply.type = 'button'; reply.title = t('Reply');
      reply.addEventListener('click', () => { closeDetail(); selectChannel(m.source); replySeq = m.seq; $('reply-text').textContent = t('Replying to ') + m.name + ': ' + m.text.slice(offset, 120); $('reply').hidden = false; $('message').focus(); }); body.append(reply);
    }
    row.append(body);
  }
  if (m.seq && source && source.canDelete && !m.deleted) {
    const mod = node('button', t('Moderate'), 'action'); mod.type = 'button';
    mod.addEventListener('click', () => {
      $('mod-target').textContent = m.name; $('mod-dialog').dataset.source = m.source;
      $('mod-dialog').dataset.seq = String(m.seq);
      $('mod-dialog').querySelectorAll('[data-mod="timeout"],[data-mod="ban"]').forEach(button => { button.hidden = !source.canBan; });
      $('mod-dialog').querySelector('[data-mod="delete"]').hidden = !source.canDelete;
      $('mod-dialog').querySelector('[data-mod="unban"]').hidden = !source.canBan || source.platform === 'youtube';
      $('mod-dialog').showModal();
    }); row.append(mod);
  }
  return row;
}
function updateSources(next) {
  const select = $('channel'), old = select.value;
  const signature = JSON.stringify(next.map(s => [s.key,s.name,s.platform,s.avatar,s.moderate,s.canBan,s.canDelete,s.editable,s.title,s.category,s.modes]));
  if (signature === JSON.stringify(sources.map(s => [s.key,s.name,s.platform,s.avatar,s.moderate,s.canBan,s.canDelete,s.editable,s.title,s.category,s.modes]))) return;
  sources = next; select.replaceChildren();
  for (const source of sources) { const option = node('option', source.name); option.value = source.key; select.append(option); }
  select.hidden = sources.length <= 1; selectChannel(old);
}
async function poll() {
  if (polling || $('chat').hidden) return;
  polling = true;
  let failed = false;
  try {
    const data = await request('/api/state?after=' + cursor + '&revision=' + revision + '&wait=1');
    updateSources(data.sources || []);
    const box = $('messages'), following = box.scrollHeight - box.scrollTop - box.clientHeight < 80;
    if (data.reset) box.replaceChildren();
    for (const message of data.messages || []) {
      if (!stripeBySeq.has(message.seq)) stripeBySeq.set(message.seq, nextStripe);
      const stripe = stripeBySeq.get(message.seq); nextStripe = !stripe;
      box.append(messageRow(message, false, stripe));
    }
    while (box.childElementCount > 250) { stripeBySeq.delete(Number(box.firstElementChild.dataset.seq)); box.firstElementChild.remove(); }
    if (data.reset) {
      const active = new Set([...box.children].map(row => Number(row.dataset.seq)));
      for (const seq of stripeBySeq.keys()) if (!active.has(seq)) stripeBySeq.delete(seq);
    }
    cursor = data.after || 0; revision = data.revision || 0; $('status').textContent = sources.map(s => s.name + ': ' + (s.modes || '')).join(' · ') || t('No open channels');
    if (detail && (data.reset || (data.messages || []).some(m => m.source === detail.source))) {
      if (!detailTimer) detailTimer = setTimeout(() => { detailTimer = null; refreshDetail(); }, 150);
    }
    if (following || data.reset) box.scrollTop = box.scrollHeight;
  } catch (error) {
    if (error.status === 401) { pairView(error.message); polling = false; return; }
    failed = true;
    $('status').textContent = t('Disconnected. Retrying…');
  }
  polling = false; timer = setTimeout(poll, failed ? 2000 : cursor ? 0 : 500);
}
function closeDetail() {
  detail = null; ++detailRequest; clearTimeout(detailTimer); detailTimer = null;
  if ($('detail-dialog').open) $('detail-dialog').close();
}
async function refreshDetail() {
  if (!detail) return;
  const current = detail, requestId = ++detailRequest;
  try {
    const mention = current.mention === undefined ? '' : '&mention=' + current.mention;
    const data = await request('/api/' + current.kind + '?source=' + encodeURIComponent(current.source) + '&seq=' + current.seq + mention);
    if (detail !== current || requestId !== detailRequest) return;
    $('detail-title').textContent = t(current.kind === 'thread' ? 'Reply thread' : 'Message history') + ' · ' + data.name + ' · ' + data.channel;
    current.name = data.name;
    $('user-actions').hidden = current.kind !== 'user-history' || !data.canModerateUser;
    $('user-actions').querySelector('[data-user-mod="unban"]').hidden = !data.canUnban;
    const box = $('detail-messages'); box.replaceChildren();
    (data.messages || []).forEach((message, index) => box.append(messageRow(message, current.kind === 'thread', index % 2 === 1)));
    if (!box.childElementCount) box.append(node('p', t('No retained messages.'), 'detail-note'));
  } catch (error) {
    if (detail === current && requestId === detailRequest) $('detail-messages').textContent = error.message;
  }
}
function showDetail(kind, message, mention) {
  if (!message.seq) return;
  detail = {kind, source:message.source, seq:message.seq, mention};
  $('detail-title').textContent = t(kind === 'thread' ? 'Reply thread' : 'Message history');
  $('detail-messages').replaceChildren();
  $('user-actions').hidden = true;
  if (!$('detail-dialog').open) $('detail-dialog').showModal();
  refreshDetail();
}
$('detail-close').addEventListener('click', closeDetail);
$('detail-dialog').addEventListener('close', () => { detail = null; ++detailRequest; clearTimeout(detailTimer); detailTimer = null; });
function showPreview(img) {
  const dialog = $('preview-dialog');
  $('preview-name').textContent = img.alt || img.title || '';
  $('preview-image').src = img.src; $('preview-image').alt = img.alt || '';
  $('preview-copy').hidden = img.dataset.preview !== 'emote';
  $('preview-status').textContent = '';
  if (!dialog.open) dialog.showModal();
}
document.addEventListener('click', event => {
  const img = event.target.closest?.('img[data-preview]');
  if (img) showPreview(img);
});
document.addEventListener('keydown', event => {
  if ((event.key === 'Enter' || event.key === ' ') && event.target.matches?.('img[data-preview]')) {
    event.preventDefault(); showPreview(event.target);
  }
});
$('preview-close').addEventListener('click', () => $('preview-dialog').close());
$('preview-copy').addEventListener('click', async () => {
  const value = $('preview-name').textContent;
  let copied = false;
  if (navigator.clipboard && window.isSecureContext) {
    try { await navigator.clipboard.writeText(value); copied = true; } catch (_) {}
  }
  if (!copied) {
    const field = document.createElement('textarea'); field.value = value; field.readOnly = true;
    field.style.position = 'fixed'; field.style.left = '0'; field.style.top = '0'; field.style.opacity = '0'; $('preview-dialog').append(field);
    field.focus(); field.select(); field.setSelectionRange(0, value.length);
    try { copied = document.execCommand('copy'); } catch (_) {}
    field.remove();
  }
  $('preview-status').textContent = t(copied ? 'Copied!' : 'Select the name to copy it.');
});
document.querySelectorAll('[data-user-mod]').forEach(button => button.addEventListener('click', async () => {
  if (!detail || !detail.name) return;
  const current = detail, action = button.dataset.userMod, duration = Number(button.dataset.duration || 0);
  if (!confirm(t('Confirm action for ') + current.name + ': ' + button.textContent + '?')) return;
  const buttons = [...$('user-actions').querySelectorAll('button')]; buttons.forEach(b => { b.disabled = true; });
  const body = {source:current.source, seq:current.seq, action, duration};
  if (current.mention !== undefined) body.mention = current.mention;
  try { await request('/api/moderate', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)}); }
  catch (error) { alert(error.message); }
  finally { buttons.forEach(b => { b.disabled = false; }); }
}));
let editingSource = null;
$('edit-channel').addEventListener('click', () => {
  const source = sources.find(s => s.key === $('channel').value);
  if (!source || !source.editable) return;
  editingSource = source.key; ++categoryRequest; clearTimeout(categoryTimer);
  $('edit-heading').textContent = t('Edit channel') + ' · ' + source.name;
  $('edit-title').value = source.title || ''; $('edit-title').maxLength = source.platform === 'youtube' ? 100 : source.platform === 'twitch' ? 140 : 200;
  $('edit-category-fields').hidden = source.platform === 'youtube';
  $('category-search').value = ''; $('category-search').placeholder = source.category || t('Search category');
  $('category-results').replaceChildren(node('option', t('Keep current category'))); $('category-results').firstElementChild.value = '';
  $('edit-error').textContent = ''; $('edit-save').disabled = false;
  $('edit-dialog').showModal();
});
$('edit-close').addEventListener('click', () => $('edit-dialog').close());
$('edit-dialog').addEventListener('close', () => { editingSource = null; ++categoryRequest; clearTimeout(categoryTimer); });
$('category-search').addEventListener('input', () => {
  clearTimeout(categoryTimer); const term = $('category-search').value.trim(), source = editingSource, requestId = ++categoryRequest;
  $('category-results').replaceChildren(node('option', t('Keep current category'))); $('category-results').firstElementChild.value = '';
  if (term.length < 2 || !source) return;
  categoryTimer = setTimeout(async () => {
    try {
      const data = await request('/api/categories?source=' + encodeURIComponent(source) + '&q=' + encodeURIComponent(term));
      if (requestId !== categoryRequest || editingSource !== source) return;
      for (const item of data.categories || []) { const option = node('option', item.name); option.value = item.id; $('category-results').append(option); }
    } catch (error) { if (requestId === categoryRequest) $('edit-error').textContent = error.message; }
  }, 400);
});
$('edit-form').addEventListener('submit', async event => {
  event.preventDefault();
  const source = sources.find(s => s.key === editingSource);
  if (!source || !source.editable) { $('edit-error').textContent = t('Channel editing is unavailable.'); return; }
  const term = $('category-search').value.trim(), categoryId = $('category-results').value;
  if (source.platform !== 'youtube' && term && !categoryId && term.toLowerCase() !== (source.category || '').toLowerCase()) {
    $('edit-error').textContent = t('Choose a category'); return;
  }
  $('edit-save').disabled = true; $('edit-error').textContent = '';
  try {
    await request('/api/channel', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source:source.key,title:$('edit-title').value,categoryId})});
    $('edit-dialog').close();
  } catch (error) { $('edit-error').textContent = error.message; }
  finally { $('edit-save').disabled = false; }
});
$('cancel-reply').addEventListener('click', () => { replySeq = 0; $('reply').hidden = true; });
$('channel').addEventListener('change', () => { replySeq = 0; $('reply').hidden = true; $('picker').hidden = true; updateEditButton(); });
$('send-form').addEventListener('submit', async event => {
  event.preventDefault(); const input = $('message'), text = input.value.trim(); if (!text || !sources.length) return;
  input.disabled = true; $('send').disabled = true;
  try { await request('/api/send', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source:$('channel').value,text,seq:replySeq})}); input.value = ''; replySeq = 0; $('reply').hidden = true; }
  catch (error) { alert(error.message); }
  finally { input.disabled = false; $('send').disabled = false; input.focus(); }
});
function drawEmotes() {
  const query = $('emote-search').value.toLowerCase(); $('emotes').replaceChildren();
  for (const item of catalog.filter(x => x.name.toLowerCase().includes(query)).slice(0, 200)) {
    const button = node('button', undefined); button.type = 'button'; button.title = item.name;
    const image = node('img'); image.src = item.url; image.alt = item.name; image.loading = 'lazy'; button.append(image, document.createTextNode(item.name));
    button.addEventListener('click', () => { const field = $('message'); field.value += (field.value && !field.value.endsWith(' ') ? ' ' : '') + item.name + ' '; field.focus(); });
    $('emotes').append(button);
  }
}
$('show-emotes').addEventListener('click', async () => {
  $('picker').hidden = !$('picker').hidden; if ($('picker').hidden) return;
  try { const data = await request('/api/emotes?source=' + encodeURIComponent($('channel').value)); catalog = data.emotes || []; drawEmotes(); }
  catch (error) { alert(error.message); }
});
$('emote-search').addEventListener('input', drawEmotes);
$('close-picker').addEventListener('click', () => { $('picker').hidden = true; });
$('mod-cancel').addEventListener('click', () => $('mod-dialog').close());
document.querySelectorAll('[data-mod]').forEach(button => button.addEventListener('click', async () => {
  const dialog = $('mod-dialog'), action = button.dataset.mod, source = dialog.dataset.source, seq = Number(dialog.dataset.seq);
  const name = $('mod-target').textContent; dialog.close();
  if (!confirm(t('Confirm action for ') + name + ': ' + button.textContent + '?')) return;
  try { await request('/api/moderate', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source,seq,action,duration:600})}); }
  catch (error) { alert(error.message); }
}));
$('disconnect').addEventListener('click', async () => { try { await request('/api/logout', {method:'POST', headers:{'Content-Type':'application/json'}, body:'{}'}); } catch (_) {} pairView(t('Disconnected.')); });
document.addEventListener('visibilitychange', () => { if (!document.hidden && !$('chat').hidden) { clearTimeout(timer); poll(); } });
const params = new URLSearchParams(location.hash.slice(1)); const code = params.get('pair');
history.replaceState(null, '', location.pathname);
request('/api/config').then(data => { language = data.language || 'en'; }).catch(() => {}).finally(() => {
  translate();
  if (code && /^[0-9]{6}$/.test(code)) { $('code').value = code; pair(code); }
  else request('/api/state?after=0').then(() => { $('pairing').hidden = true; $('chat').hidden = false; poll(); }).catch(() => pairView());
});
