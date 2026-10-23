"""Existing feature checks use mocked transports, never real chat actions."""
import io
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import test_chat as base
from ytchatterino_features import channel_browser_url

core=base.core


class FeatureRegressionTests(unittest.TestCase):
    setUp=base.UiTests.setUp
    tearDown=base.UiTests.tearDown
    msg=base.UiTests.msg

    def test_twitch_sending_and_reply_payload(self):
        owner=self.app.features
        with patch.object(owner,'twitch_identity',return_value={'user_id':'42'}),patch.object(owner,'broadcaster',return_value='77'),patch.object(owner,'helix',return_value={'data':[{'is_sent':True,'message_id':'new'}]}) as api:
            owner.send(self.source,'mocked reply',reply={'id':'parent'})
            self.assertEqual(api.call_args.kwargs['body']['reply_parent_message_id'],'parent')
            self.assertEqual(api.call_args.kwargs['scope'],'user:write:chat')

    def test_twitch_moderation_permission_and_durations(self):
        owner=self.app.features;message={'id':'one','user_id':'55'}
        with patch.object(owner,'can_moderate_twitch',return_value=False),self.assertRaises(RuntimeError):
            owner.moderation_request(self.source,message,'delete')
        with patch.object(owner,'can_moderate_twitch',return_value=True),patch.object(owner,'broadcaster',return_value='77'),patch.object(owner,'twitch_identity',return_value={'user_id':'42'}),patch.object(owner,'helix') as api:
            owner.moderation_request(self.source,message,'timeout',60)
            self.assertEqual(api.call_args.kwargs['body']['data']['duration'],60)
            owner.moderation_request(self.source,message,'delete')
            self.assertEqual(api.call_args.args,('moderation/chat','DELETE'))
            owner.moderation_request(self.source,message,'unban')
            self.assertEqual(api.call_args.kwargs['params']['user_id'],'55')
            with self.assertRaises(RuntimeError):owner.moderation_request(self.source,{'id':'own','user_id':'77'},'delete')

    def test_youtube_sending_moderation_and_quota_costs(self):
        owner=self.app.features;source=SimpleNamespace(platform='youtube',key='yt',video_id='12345678901')
        with patch.object(owner,'youtube_chat_id',return_value='chat'),patch.object(owner,'youtube_moderation_id',return_value='api-id'),patch.object(owner,'youtube',return_value={'id':'new'}) as api:
            owner.send(source,'mocked message')
            self.assertEqual(api.call_args.kwargs['units'],20)
            self.assertEqual(api.call_args.kwargs['body']['snippet']['textMessageDetails']['messageText'],'mocked message')
            owner.moderation_request(source,{'id':'one','user_id':'UCuser'},'delete')
            self.assertEqual(api.call_args.kwargs['units'],20)
            owner.moderation_request(source,{'id':'one','user_id':'UCuser'},'timeout',30)
            self.assertEqual(api.call_args.kwargs['units'],200)
            self.assertEqual(api.call_args.kwargs['body']['snippet']['banDurationSeconds'],30)

    def test_title_and_category_permissions_and_youtube_metadata(self):
        owner=self.app.features
        with patch.object(owner,'broadcaster',return_value='77'),patch.object(owner,'twitch_identity',return_value={'user_id':'42'}),self.assertRaises(RuntimeError):
            owner.update_stream(self.source,'title','category')
        with patch.object(owner,'broadcaster',return_value='42'),patch.object(owner,'twitch_identity',return_value={'user_id':'42'}),patch.object(owner,'helix') as api:
            owner.update_stream(self.source,'title','category')
            self.assertEqual(api.call_args.kwargs['body'],{'title':'title','game_id':'category'})
        source=SimpleNamespace(platform='youtube',video_id='12345678901')
        snippet={'title':'old','categoryId':'20','description':'keep','tags':['one']}
        with patch.object(owner,'youtube',return_value={'items':[{'snippet':snippet}]}) as api:
            owner.update_stream(source,'new')
            self.assertEqual(api.call_args.kwargs['body']['snippet']['description'],'keep')
            self.assertEqual(api.call_args.kwargs['units'],50)

    def test_saved_channels_deduplicate_and_sounds_only_on_live_transition(self):
        owner=self.app.features
        owner.save_source(self.source);owner.save_source(self.source)
        self.assertEqual(len(self.app.cfg['saved_channels']),1)
        with patch.object(owner,'play_sound') as sound:
            owner.live_changed('twitch:test',True);sound.assert_not_called()
            owner.live_changed('twitch:test',False)
            owner.live_changed('twitch:test',True)
            sound.assert_called_once_with(live=True)

    def test_emote_completion_and_picker_catalog(self):
        owner=self.app.features
        self.source.emote_map={'Kappa':{'scope':'global'},'Kappu':{'scope':'channel'}}
        self.pane.compose_text.set('Kap');self.pane.compose_entry.icursor('end')
        self.assertEqual(owner.complete_name(self.pane),'break')
        self.assertEqual(self.pane.compose_text.get(),'Kappa ')
        owner.complete_name(self.pane)
        self.assertEqual(self.pane.compose_text.get(),'Kappu ')
        self.assertIn('Kappa',owner.available_emotes(self.source))

    def test_public_browser_links_never_include_credentials(self):
        for kind in ('stream','player','chat'):
            url=channel_browser_url(self.source,kind)
            self.assertIn('guicraft_',url)
            self.assertNotIn('token',url)
        youtube=SimpleNamespace(platform='youtube',video_id='12345678901')
        self.assertIn('live_chat',channel_browser_url(youtube,'chat'))

    def test_layout_mixing_search_and_session_serialization(self):
        with patch.object(core.ChatSource,'ensure_worker'):
            second=self.app.source_for({'platform':'youtube','id':'12345678901'})
            self.pane.set_sources([self.source,second])
        self.assertEqual(len(self.pane.sources),2)
        self.pane.add(self.msg(1,'Find this word'),self.source)
        self.assertEqual(self.pane.set_search('word'),1)
        with patch.object(core,'save_config') as saved:
            self.app.save_session()
            self.assertEqual(len(saved.call_args.args[0]['session']['sources']),2)

    def test_animated_gif_decoder_keeps_frames_and_timing(self):
        from PIL import Image
        first=Image.new('RGBA',(3,3),'red');second=Image.new('RGBA',(3,3),'blue')
        output=io.BytesIO()
        first.save(output,format='GIF',save_all=True,append_images=[second],duration=[40,100],loop=0)
        frames,timings=core._gif_composited_png_frames(output.getvalue(),with_timing=True)
        self.assertEqual(len(frames),2)
        self.assertNotEqual(frames[0],frames[1])
        self.assertEqual(timings,[40,100])


if __name__=='__main__':unittest.main()
