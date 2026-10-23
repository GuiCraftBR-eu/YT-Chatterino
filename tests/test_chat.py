import importlib.machinery
import importlib.util
import json
from pathlib import Path
import sys
import time
import tkinter as tk
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
loader = importlib.machinery.SourceFileLoader('ytchatterino_test_core', str(ROOT/'src'/'ytchatterino.pyw'))
spec = importlib.util.spec_from_loader(loader.name, loader)
core = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = core
loader.exec_module(core)
from ytchatterino_chat import message_limit, same_user, recent_twitch
from ytchatterino_features import FeatureController
from ytchatterino_youtube import YouTubeWebClient


def irc(mid='one', body='Hello Kappa', reply=''):
    return ('@id='+mid+';user-id=42;room-id=12;display-name=Tester;color=#00ff00;'
            'badges=subscriber/12;badge-info=subscriber/73;tmi-sent-ts=1700000000000;emotes=25:6-10'
            +reply+' :tester!tester@tester PRIVMSG #guicraft_ :'+body)


class ParserTests(unittest.TestCase):
    def test_reply_fields_and_badges(self):
        channel, m = core.TwitchIRCManager.parse_message(irc(reply=';reply-parent-msg-id=parent;reply-parent-user-login=someone;reply-parent-display-name=Someone;reply-parent-msg-body=hello\\sworld'))
        self.assertEqual(channel, 'guicraft_')
        self.assertEqual(m['reply']['text'], 'hello world')
        self.assertEqual(m['reply']['id'], 'parent')
        self.assertEqual(m['badge_info'], 'subscriber/73')
        self.assertEqual(m['native_emotes'][0]['start'], 6)

    def test_missing_twitch_fields_do_not_crash(self):
        channel,m=core.TwitchIRCManager.parse_message(':tester!t@t PRIVMSG #guicraft_ :plain')
        self.assertEqual(m['name'],'tester')
        self.assertIsNone(m['reply'])
        self.assertEqual(m['native_emotes'],[])

    def test_caps_and_invalid_settings(self):
        self.assertEqual(message_limit({}), 2000)
        self.assertEqual(message_limit({'message_limit': 'wrong'}), 2000)
        self.assertEqual(message_limit({'message_limit': 50000}), 20000)
        self.assertEqual(message_limit({'message_limit': 2000,'chat_message_limits':{'a':350}},'a'),350)

    def test_history_filters_and_dedup(self):
        response = SimpleNamespace(read=lambda size:json.dumps({'messages':[irc(),irc(),irc().replace('#guicraft_', '#other')]}).encode())
        class Response:
            def __enter__(self):return response
            def __exit__(self,*args):pass
        with patch('urllib.request.urlopen',return_value=Response()) as request:
            messages=recent_twitch('guicraft_',200,core.TwitchIRCManager.parse_message)
        self.assertEqual(len(messages),1)
        self.assertTrue(messages[0]['_history'])
        self.assertNotIn('Authorization',request.call_args.args[0].headers)

    def test_youtube_initial_messages(self):
        renderer={'id':'yt-old','authorName':{'simpleText':'YT user'},'authorExternalChannelId':'UCabc',
                  'timestampUsec':'1700000000000000','message':{'runs':[{'text':'past'}]}}
        from ytchatterino_youtube import parse_renderer
        result=parse_renderer(renderer,'liveChatTextMessageRenderer',historical=True)
        self.assertTrue(result['_history'])
        self.assertEqual(result['text'],'past')
        client=YouTubeWebClient()
        client.video_id='12345678901';client.continuation='a';client.context={'client':{'clientVersion':'1'}}
        client.initial_events=[('msg',result)]
        client.request=lambda *a,**kw:{'continuationContents':{'liveChatContinuation':{'continuations':[{'timedContinuationData':{'continuation':'b','timeoutMs':2000}}]}}}
        self.assertEqual(len(client.poll()[0]),1)
        self.assertEqual(len(client.poll()[0]),0)

    def test_send_reply_payload(self):
        owner=FeatureController.__new__(FeatureController)
        owner.last_send={}
        owner.twitch_identity=lambda:{'user_id':'42'}
        owner.broadcaster=lambda source:'12'
        calls=[]
        owner.helix=lambda *a,**kw:(calls.append(kw) or {'data':[{'is_sent':True,'message_id':'new'}]})
        owner.send(SimpleNamespace(key='tw:one',platform='twitch'),'reply text',{'id':'parent'})
        self.assertEqual(calls[0]['body']['reply_parent_message_id'],'parent')
        self.assertTrue(same_user({'user_id':'42','name':'New'}, {'user_id':'42','name':'Old'}))

    def test_linux_keyring_does_not_save_plaintext(self):
        store=core.TwitchTokenStore();store.windows=False
        bad=SimpleNamespace(__class__=type('Plaintext',(),{}))
        fake=SimpleNamespace(get_keyring=lambda:bad,set_password=lambda *a:self.fail('Plaintext save attempted'))
        with patch.dict(sys.modules,keyring=fake):
            with self.assertRaises(OSError):store.write('not-a-real-token')

    def test_linux_secure_chained_keyring(self):
        secure=type('Keyring',(),{'__module__':'keyring.backends.SecretService',
            'get_password':lambda self,*a:'mock-value','set_password':lambda self,*a:None})()
        chained=type('ChainerBackend',(),{'__module__':'keyring.backends.chainer','backends':[secure]})()
        store=core.TwitchTokenStore();store.windows=False
        with patch.dict(sys.modules,keyring=SimpleNamespace(get_keyring=lambda:chained)):
            self.assertEqual(store.read(),'mock-value')
            self.assertTrue(store.write('mock-value'))


class UiTests(unittest.TestCase):
    def setUp(self):
        self.root=tk.Tk();self.root.withdraw()
        self.patches=[patch.object(core,'load_config',return_value={'features':{'alerts':{'enabled':False}}}),
            patch.object(core,'save_config'),patch.object(core.App,'_read_twitch_auth',return_value=None),
            patch.object(core.FeatureController,'start')]
        for p in self.patches:p.start()
        self.app=core.App(self.root)
        self.source=self.app.source_for({'platform':'twitch','id':'guicraft_'})
        self.workspace=self.app.new_workspace([self.source]);self.pane=self.workspace.panes[0]
        self.pane.active=True

    def tearDown(self):
        self.app.quit()
        for p in reversed(self.patches):p.stop()

    def msg(self,i,text='message'):
        return {'id':str(i),'name':'Tester','login':'tester','user_id':'42','time':'1:00 pm',
                'text':text,'badges':[],'special':None,'_seq':i}

    def test_exact_default_cap_multiline_and_cleanup(self):
        self.pane.clear_messages(True)
        start=time.monotonic()
        for i in range(1,2201):self.pane.add(self.msg(i,'line1\nline2' if i%2 else 'short'),self.source)
        self.assertEqual(len(self.pane.row_marks),2000)
        self.assertEqual(len(self.pane.message_records),2000)
        self.assertNotIn(200,self.pane.message_records)
        self.assertIn(201,self.pane.message_records)
        self.assertLess(time.monotonic()-start,20)
        self.app.cfg['chat_message_limits']={self.pane.setting_key:100}
        self.app.features.chat_tools.apply_limits()
        self.assertEqual(len(self.pane.row_marks),100)
        self.assertEqual(len(self.pane.message_records),100)
        self.assertLessEqual(len([m for m in self.pane.text.mark_names() if m.startswith('row_')]),100)

    def test_image_references_are_removed_with_old_rows(self):
        self.pane.clear_messages(True)
        self.app.cfg['chat_message_limits']={self.pane.setting_key:100}
        self.pane.add(self.msg(1),self.source)
        photo=tk.PhotoImage(master=self.root,width=2,height=2)
        self.pane.text.configure(state='normal')
        image_name=self.pane.text.image_create('1.3',image=photo)
        self.pane.image_names[image_name]='Kappa';self.pane.image_assets[image_name]=('https://example.invalid/emote',)
        self.pane.text.configure(state='disabled')
        for i in range(2,105):self.pane.add(self.msg(i),self.source)
        self.assertNotIn(image_name,self.pane.image_names)
        self.assertNotIn(image_name,self.pane.image_assets)
        self.assertEqual(len(self.pane.row_color_tags),100)

    def test_reply_cancel_and_usercard(self):
        m=self.msg(1)
        self.source.publish('msg',m)
        tools=self.app.features.chat_tools
        tools.start_reply(self.pane,self.source,m)
        self.assertEqual(self.pane.reply_target[1]['id'],'1')
        tools.cancel_reply(self.pane)
        self.assertIsNone(self.pane.reply_target)
        with patch.object(tools,'public_profile',return_value={}),patch.object(self.app.features,'async_action'):
            tools.show_user(self.pane,self.source,m)
        card=next(iter(tools.cards.values()))
        self.assertIn('Tester',card.text.get('1.0','end'))
        self.source.publish('msg',self.msg(2,'new message'))
        card.refresh_messages()
        self.assertIn('new message',card.text.get('1.0','end'))
        card.close();self.assertFalse(tools.cards)

    def test_history_before_live_and_seen_limit(self):
        self.source.history_loading=True
        self.source.publish('msg',self.msg(2,'live'))
        self.assertEqual(len(self.source.history_live),1)
        self.source.history_loading=False
        self.source.publish('msg',dict(self.msg(1,'old'),_history=True))
        self.source.publish('msg',self.source.history_live.popleft())
        self.source.publish('msg',self.msg(2,'duplicate'))
        self.assertEqual([m['id'] for _,k,m,_ in self.source.history if k=='msg'],['1','2'])


if __name__=='__main__':unittest.main(verbosity=2)

