"""Anonymous YouTube website chat reader. No Google project key or credentials.

Only public watch/channel pages and the website's live-chat continuation endpoint
are read. Sending and moderation remain in the authenticated feature module.
"""
from datetime import datetime
import http.cookiejar
import json
import re
import urllib.error
import urllib.parse
import urllib.request

USER_AGENT = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) '
              'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36')
WEB_POLL_SECONDS = 2.0
MAX_RESPONSE = 8 * 1024 * 1024


class YouTubeReadError(RuntimeError):
    def __init__(self, message, terminal=False, retry=30):
        super().__init__(message)
        self.terminal, self.retry = terminal, retry


def walk(value):
    """Iterate JSON containers without recursion depth dependence."""
    stack = [value]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            yield node
            stack.extend(reversed(list(node.values())))
        elif isinstance(node, list):
            stack.extend(reversed(node))


def text_of(value):
    if isinstance(value, str):
        return value
    if not isinstance(value, dict):
        return ''
    return str(value.get('simpleText') or value.get('content') or ''.join(
        str(run.get('text', '')) for run in value.get('runs', []) if isinstance(run, dict)))


def embedded_json(html, name):
    patterns = (r'(?:var\s+)?' + re.escape(name) + r'\s*=\s*',
                r'window\[[\"\x27]' + re.escape(name) + r'[\"\x27]\]\s*=\s*')
    decoder = json.JSONDecoder()
    for pattern in patterns:
        for match in re.finditer(pattern, html):
            try:
                value, _ = decoder.raw_decode(html[match.end():])
                if isinstance(value, dict):
                    return value
            except ValueError:
                continue
    return {}


def web_config(html):
    config = {}
    decoder = json.JSONDecoder()
    for match in re.finditer(r'ytcfg\.set\(\s*', html):
        try:
            item, _ = decoder.raw_decode(html[match.end():])
            if isinstance(item, dict):
                config.update(item)
        except ValueError:
            pass
    return config


def image_urls(value):
    if not isinstance(value, dict):
        return ()
    records = value.get('thumbnails') or value.get('sources') or []
    urls = []
    for item in sorted((v for v in records if isinstance(v, dict)),
                       key=lambda v: int(v.get('width') or 0), reverse=True):
        url = item.get('url', '')
        if url.startswith('//'):
            url = 'https:' + url
        if url.startswith('https://') and url not in urls:
            urls.append(url)
    return tuple(urls)


def message_runs(value):
    """Return text and image spans using Python offsets (including emoji)."""
    if not isinstance(value, dict) or not value.get('runs'):
        return text_of(value), []
    parts, spans, offset = [], [], 0
    for run in value['runs']:
        if not isinstance(run, dict):
            continue
        emoji = run.get('emoji')
        if isinstance(emoji, dict):
            shortcuts = emoji.get('shortcuts') or []
            label = shortcuts[0] if shortcuts else emoji.get('emojiId') or '[emote]'
            urls = image_urls(emoji.get('image'))
            if urls:
                spans.append({'start': offset, 'end': offset + len(label), 'urls': urls,
                              'source': 'youtube', 'id': emoji.get('emojiId'), 'code': label})
        else:
            label = str(run.get('text', ''))
        parts.append(label)
        offset += len(label)
    return ''.join(parts), spans


def parse_renderer(renderer, kind, historical=False):
    name = text_of(renderer.get('authorName'))
    if not name or not renderer.get('id'):
        return None
    text, emotes = message_runs(renderer.get('message') or renderer.get('headerSubtext') or {})
    special = None
    if kind in ('liveChatPaidMessageRenderer', 'liveChatPaidStickerRenderer'):
        special = 'super'
        amount = text_of(renderer.get('purchaseAmountText'))
        if not text and kind == 'liveChatPaidStickerRenderer':
            text = '[sticker]'
            urls = image_urls(renderer.get('sticker'))
            if urls:
                emotes = [{'start': 0, 'end': len(text), 'urls': urls, 'source': 'youtube'}]
        if amount:
            prefix = amount + ' '
            for emote in emotes:
                emote['start'] += len(prefix)
                emote['end'] += len(prefix)
            text = prefix + text
    elif kind in ('liveChatMembershipItemRenderer', 'liveChatSponsorshipsGiftPurchaseAnnouncementRenderer',
                  'liveChatSponsorshipsGiftRedemptionAnnouncementRenderer'):
        special = 'member'
        text = text or text_of(renderer.get('headerPrimaryText')) or 'became / renewed as a member'
    if not text:
        return None
    badges, images = [], []
    role_types = {'OWNER': ('[HOST]', 'host'), 'MODERATOR': ('[MOD]', 'moderator'),
                  'VERIFIED': ('[VERIFIED]', 'verified')}
    for value in renderer.get('authorBadges', []):
        badge = value.get('liveChatAuthorBadgeRenderer', {})
        icon = badge.get('icon', {}).get('iconType', '')
        urls = image_urls(badge.get('customThumbnail'))
        label = badge.get('tooltip') or badge.get('accessibility', {}).get('accessibilityData', {}).get('label', '')
        if icon in role_types:
            token, role = role_types[icon]
        elif urls or 'member' in label.lower():
            token, role = '[MEMBER]', 'member'
        else:
            continue
        if token not in badges:
            badges.append(token)
            images.append({'urls': urls, 'label': token, 'role': role})
    sent_at = None
    try:
        sent_at = int(renderer.get('timestampUsec', 0)) / 1_000_000 or None
    except (TypeError, ValueError):
        pass
    stamp = datetime.fromtimestamp(sent_at) if sent_at else datetime.now()
    result = {'id': renderer['id'], 'name': name,
              'user_id': renderer.get('authorExternalChannelId'),
              'avatar': next(iter(image_urls(renderer.get('authorPhoto'))), None),
              'time': stamp.strftime('%I:%M %p').lower().lstrip('0'), 'text': text,
              'badges': badges, 'youtube_badges': images, 'native_emotes': emotes,
              'special': special, '_history': historical, '_youtube_web': True}
    if sent_at:
        result['sent_at'] = sent_at
    return result


def parse_actions(actions, historical=False):
    """Ignore presentation/ticker duplicates; retain moderation events."""
    for action in actions or []:
        if not isinstance(action, dict):
            continue
        add = action.get('addChatItemAction', {})
        item = add.get('item', {})
        for kind, renderer in item.items():
            if isinstance(renderer, dict) and kind in (
                    'liveChatTextMessageRenderer', 'liveChatPaidMessageRenderer',
                    'liveChatPaidStickerRenderer', 'liveChatMembershipItemRenderer',
                    'liveChatSponsorshipsGiftPurchaseAnnouncementRenderer',
                    'liveChatSponsorshipsGiftRedemptionAnnouncementRenderer'):
                message = parse_renderer(renderer, kind, historical)
                if message:
                    yield 'msg', message
        deleted = action.get('markChatItemAsDeletedAction') or action.get('removeChatItemAction')
        if deleted and deleted.get('targetItemId'):
            yield 'moderation', {'message_id': deleted['targetItemId']}
        banned = action.get('markChatItemsByAuthorAsDeletedAction', {})
        if banned.get('externalChannelId'):
            yield 'moderation', {'user_id': banned['externalChannelId']}


def continuation_info(renderer):
    for item in renderer.get('continuations', []):
        for name in ('invalidationContinuationData', 'timedContinuationData', 'reloadContinuationData'):
            value = item.get(name, {})
            if value.get('continuation'):
                try:
                    wait = max(WEB_POLL_SECONDS, min(120, float(value.get('timeoutMs', 5000)) / 1000))
                except (TypeError, ValueError):
                    wait = 5
                return value['continuation'], wait
    return None, 5


class YouTubeWebClient:
    def __init__(self):
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.context, self.continuation, self.visitor = {}, None, None
        self.video_id = None
        self.initial = True
        self.requests = 0

    def request(self, url, body=None, headers=None):
        # No account cookies, Google API key, or OAuth access token are loaded.
        fields = {'User-Agent': USER_AGENT, 'Accept-Language': 'en-US,en;q=0.9'}
        fields.update(headers or {})
        data = None
        if body is not None:
            data = json.dumps(body).encode('utf-8')
            fields['Content-Type'] = 'application/json'
        req = urllib.request.Request(url, data=data, headers=fields)
        self.requests += 1
        try:
            with self.opener.open(req, timeout=20) as response:
                if urllib.parse.urlparse(response.url).hostname not in ('www.youtube.com', 'youtube.com'):
                    raise YouTubeReadError('YouTube needs browser confirmation; open this stream in your browser.', retry=120)
                raw = response.read(MAX_RESPONSE + 1)
                if len(raw) > MAX_RESPONSE:
                    raise YouTubeReadError('YouTube returned an oversized response.', retry=60)
        except urllib.error.HTTPError as error:
            if error.code == 429:
                raise YouTubeReadError('YouTube temporarily rate-limited chat reading. Retrying in 2 minutes.', retry=120) from None
            if error.code in (401, 403):
                raise YouTubeReadError('YouTube restricted anonymous reading. Use Open chat in browser.', retry=120) from None
            raise YouTubeReadError('YouTube chat request failed (HTTP ' + str(error.code) + ').', retry=30) from None
        except (urllib.error.URLError, TimeoutError, OSError):
            raise YouTubeReadError('Could not reach YouTube. Check your connection.', retry=15) from None
        if body is not None:
            try:
                return json.loads(raw)
            except ValueError:
                raise YouTubeReadError('YouTube returned an unreadable chat response.', retry=30) from None
        return raw.decode('utf-8', errors='replace')

    def watch_info(self, video_id):
        if not re.fullmatch(r'[a-zA-Z0-9_-]{11}', str(video_id)):
            raise YouTubeReadError('Video not found. Check the link.', terminal=True)
        html = self.request('https://www.youtube.com/watch?v=' + video_id)
        return self.metadata(html, video_id)

    @staticmethod
    def metadata(html, expected_id=None):
        player = embedded_json(html, 'ytInitialPlayerResponse')
        details = player.get('videoDetails', {})
        if not details:
            reason = text_of(player.get('playabilityStatus', {}).get('reason'))
            raise YouTubeReadError(reason or 'YouTube video is unavailable to anonymous viewers.', retry=60)
        if expected_id and details.get('videoId') != expected_id:
            raise YouTubeReadError('YouTube returned a different video. Check the live link.', terminal=True)
        initial = embedded_json(html, 'ytInitialData')
        live = bool(details.get('isLive'))
        viewers = None
        if live:
            for node in walk(initial):
                view = node.get('videoViewCountRenderer', {})
                if view.get('isLive'):
                    digits = re.sub(r'[^0-9]', '', text_of(view.get('viewCount')))
                    if digits:
                        viewers = int(digits)
                        break
            if viewers is None:
                try:
                    viewers = int(details.get('viewCount'))
                except (TypeError, ValueError):
                    pass
        info = {'channel': details.get('author', ''), 'channel_id': details.get('channelId'),
                'title': details.get('title', ''), 'viewers': viewers, 'live': live,
                'reader': 'web'}
        return info, initial, web_config(html)

    def connect(self, video_id):
        if not re.fullmatch(r'[a-zA-Z0-9_-]{11}', str(video_id)):
            raise YouTubeReadError('Video not found. Check the link.', terminal=True)
        self.video_id = video_id
        html = self.request('https://www.youtube.com/watch?v=' + video_id)
        info, initial, config = self.metadata(html, video_id)
        self.info = info
        chat = next((node['liveChatRenderer'] for node in walk(initial) if 'liveChatRenderer' in node), None)
        if not chat:
            if not info['live']:
                raise YouTubeReadError('No active live chat. The broadcast is offline, ended, or scheduled.', retry=60)
            raise YouTubeReadError('Live chat is disabled or unavailable to anonymous viewers.', retry=60)
        # The popout page supplies complete filter continuations. Watch-page
        # filter continuations can be abbreviated, so do not synthesize tokens.
        popup = self.request('https://www.youtube.com/live_chat?v=' + video_id + '&is_popout=1')
        popup_data = embedded_json(popup, 'ytInitialData')
        full_chat = popup_data.get('contents', {}).get('liveChatRenderer', {})
        if full_chat:
            chat = full_chat
            config.update(web_config(popup))
        self.context = config.get('INNERTUBE_CONTEXT', {})
        if not self.context.get('client', {}).get('clientVersion'):
            raise YouTubeReadError('YouTube changed its chat page. Open chat in browser until the app is updated.', retry=120)
        self.continuation, _ = continuation_info(chat)
        selectors = chat.get('header', {}).get('liveChatHeaderRenderer', {}).get('viewSelector', {}).get('sortFilterSubMenuRenderer', {}).get('subMenuItems', [])
        for item in selectors:
            if item.get('title', '').casefold() == 'live chat':
                token = item.get('continuation', {}).get('reloadContinuationData', {}).get('continuation')
                if token and len(token) > 100:
                    self.continuation = token
        if not self.continuation:
            raise YouTubeReadError('YouTube did not provide a live chat continuation.', retry=60)
        self.initial = True
        self.initial_events = list(parse_actions(chat.get('actions'), historical=True))
        if not self.initial_events:
            for node in chat.get('items', []):
                for kind, renderer in node.items():
                    if isinstance(renderer, dict):
                        message = parse_renderer(renderer, kind, historical=True)
                        if message:
                            self.initial_events.append(('msg', message))
        return info

    def poll(self):
        headers = {'Origin': 'https://www.youtube.com',
                   'Referer': 'https://www.youtube.com/live_chat?v=' + self.video_id,
                   'X-Youtube-Client-Name': '1',
                   'X-Youtube-Client-Version': self.context['client']['clientVersion']}
        if self.visitor:
            headers['X-Goog-Visitor-Id'] = self.visitor
        response = self.request('https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?prettyPrint=false',
                                {'context': self.context, 'continuation': self.continuation}, headers)
        self.visitor = response.get('responseContext', {}).get('visitorData') or self.visitor
        chat = response.get('continuationContents', {}).get('liveChatContinuation', {})
        if not chat:
            raise YouTubeReadError('The live chat connection expired or ended. Reconnecting…', retry=15)
        token, wait = continuation_info(chat)
        if not token:
            raise YouTubeReadError('The live chat has ended.', terminal=True)
        self.continuation = token
        events = list(getattr(self, 'initial_events', [])) + list(parse_actions(chat.get('actions'), historical=self.initial))
        self.initial_events = []
        self.initial = False
        return events, wait

    def resolve_channel(self, kind, value):
        if kind == 'id' and re.fullmatch(r'UC[\w-]{22}', value):
            path = 'channel/' + value
        elif kind in ('handle', 'username') and value and len(value) <= 100:
            path = ('@' if kind == 'handle' else 'user/') + urllib.parse.quote(value.lstrip('@'), safe='')
        else:
            raise ValueError('Enter a valid YouTube channel URL, @handle, or UC channel ID.')
        data = embedded_json(self.request('https://www.youtube.com/' + path), 'ytInitialData')
        metadata = data.get('metadata', {}).get('channelMetadataRenderer', {})
        channel_id = metadata.get('externalId')
        if not channel_id or not re.fullmatch(r'UC[\w-]{22}', channel_id):
            raise YouTubeReadError('YouTube channel not found.', terminal=True)
        return {'platform': 'youtube', 'channel_id': channel_id, 'title': metadata.get('title', value)}

    def live_streams(self, channel_id):
        if not re.fullmatch(r'UC[\w-]{22}', str(channel_id)):
            raise ValueError('This saved YouTube channel has an invalid ID.')
        html = self.request('https://www.youtube.com/channel/' + channel_id + '/streams')
        data = embedded_json(html, 'ytInitialData')
        streams = {}
        for node in walk(data):
            for kind in ('videoRenderer', 'gridVideoRenderer', 'lockupViewModel'):
                item = node.get(kind)
                if not isinstance(item, dict):
                    continue
                vid = item.get('videoId') or item.get('contentId')
                if not re.fullmatch(r'[a-zA-Z0-9_-]{11}', str(vid)):
                    continue
                live = any(child.get('style') == 'LIVE' or child.get('text') == 'LIVE'
                           or child.get('label') == 'LIVE' for child in walk(item))
                if live:
                    title = text_of(item.get('title'))
                    if not title:
                        title = next((text_of(c.get('title')) for c in walk(item) if c.get('title')), vid)
                    streams[vid] = {'id': {'videoId': vid}, 'snippet': {'title': title or vid}}
        if not streams:
            # Redirects to one current broadcast when the streams tab has a new
            # layout. Only an explicitly opened save triggers this lookup.
            live_html = self.request('https://www.youtube.com/channel/' + channel_id + '/live')
            player = embedded_json(live_html, 'ytInitialPlayerResponse').get('videoDetails', {})
            if player.get('isLive') and re.fullmatch(r'[a-zA-Z0-9_-]{11}', player.get('videoId', '')):
                vid = player['videoId']
                streams[vid] = {'id': {'videoId': vid}, 'snippet': {'title': player.get('title', vid)}}
        return list(streams.values())
