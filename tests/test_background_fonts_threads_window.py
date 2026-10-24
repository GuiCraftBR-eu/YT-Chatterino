from collections import OrderedDict
import sys
import time
import tkinter as tk
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import test_chat as base
from ytchatterino_i18n import set_language, set_datetime_formats, display_time
from ytchatterino_threads import thread_root, collect_thread
from ytchatterino_window import WindowState, parse_geometry, valid_rect
from ytchatterino_settings import settings_matches

core = base.core


def msg(mid, parent=None, root=None, seq=1):
    return dict(id=mid, _seq=seq, name='Tester', login='tester', user_id='42',
                time='13:02:03', text='message '+mid, special=None, badges=[],
                reply=dict(id=parent, thread_id=root, name='Parent', login='parent', text='context') if parent else None)


class DataTests(unittest.TestCase):
    def test_nested_explicit_root_and_older_parent_only_history(self):
        messages = OrderedDict((m['id'], m) for m in [msg('a'), msg('b','a'), msg('c','b')])
        self.assertEqual(thread_root(messages['c'],messages),'a')
        self.assertEqual(thread_root(msg('z','missing','a'),messages),'a')
        history = [(m['_seq'],'msg',m,None) for m in messages.values()]+[(4,'msg',msg('other'),None)]
        root, found = collect_thread(history,messages['c'])
        self.assertEqual(root,'a');self.assertEqual([m['id'] for m in found],['a','b','c'])

    def test_cycles_missing_roots_and_out_of_order_history(self):
        messages={'a':msg('a','b'),'b':msg('b','a')}
        self.assertEqual(thread_root(messages['a'],messages),'a')
        self.assertEqual(thread_root(messages['b'],messages),'a')
        a,b,c=msg('a'),msg('b','a'),msg('c','b','a')
        root,found=collect_thread([(1,'msg',c,None),(2,'msg',b,None),(3,'msg',a,None)],c)
        self.assertEqual(root,'a');self.assertEqual({m['id'] for m in found},{'a','b','c'})
        self.assertEqual(thread_root(msg('x','missing'),{}),'missing')

    def test_legacy_geometry_negative_monitors_and_invalid_data(self):
        self.assertEqual(parse_geometry('880x720+-1066+158'),(880,720,-1066,158))
        self.assertEqual(parse_geometry('880x720-1066-158'),(880,720,-1066,-158))
        for bad in ('','a','10x20+0+0','880x720+999999+0','880x720+0+0oops'):
            self.assertIsNone(parse_geometry(bad))
        self.assertTrue(valid_rect([-1066,158,-186,878]))
        self.assertFalse(valid_rect([0,0,1,5]));self.assertFalse(valid_rect(['0',0,500,500]))

    def test_font_migration_clamping_and_lowercase_legacy_preferences(self):
        self.assertEqual(core.font_sizes({'font_size':17}),(17,17))
        self.assertEqual(core.font_sizes({'message_font_size':99,'username_font_size':2}),(48,7))
        self.assertEqual(core.font_sizes({'message_font_size':None,'username_font_size':'bad'}),(12,12))
        set_datetime_formats({'time_format':'hh:mm:ss AM/PM'})
        self.assertEqual(display_time('00:01:04'),'12:01:04 am')
        self.assertEqual(display_time('13:01:04'),'1:01:04 pm')
        set_datetime_formats({})
        self.assertIn(('Display','Username font size (pt)'),settings_matches('fonte nome'))


class UiTests(unittest.TestCase):
    setUp=base.UiTests.setUp
    tearDown=base.UiTests.tearDown

    def drain(self,pane=None):
        pane=pane or self.pane
        pane.active=True
        while pane.pending_events:
            pane.finish_resize();pane.drain(time.monotonic()+2,1000)

    def publish(self,message):
        self.source.publish('msg',message)
        return self.source.history[-1][2]

    def thread(self):
        self.pane.clear_messages(True)
        a=self.publish(msg('a'))
        b=self.publish(msg('b','a','a'))
        c=self.publish(msg('c','b','a'))
        self.publish(msg('other'))
        thread=self.app.features.chat_tools.show_thread(self.source,c)
        self.drain(thread.pane)
        return thread,a,b,c

    def test_youtube_keeps_polling_when_hidden_but_manual_pause_still_works(self):
        with patch.object(core.ChatSource,'ensure_worker'):
            yt=self.app.source_for({'platform':'youtube','id':'abcdefghijk'})
        worker=Mock();yt.worker=worker
        self.assertFalse(yt.views)
        yt.sync_pause();worker.set_paused.assert_called_with(False)
        yt.manual_paused=True;yt.sync_pause();worker.set_paused.assert_called_with(True)
        yt.manual_paused=False;yt.sync_pause();worker.set_paused.assert_called_with(False)
        yt.worker=None

    def test_font_changes_preserve_messages_images_selection_and_independent_names(self):
        self.pane.clear_messages(True)
        self.pane.add(msg('a'),self.source)
        photo=tk.PhotoImage(master=self.root,width=3,height=3)
        self.pane.text.configure(state='normal')
        image=self.pane.text.image_create('1.0',image=photo)
        self.pane.text.tag_add('sel','1.2','1.8');self.pane.text.configure(state='disabled')
        before=self.pane.text.get('1.0','end');selection=tuple(map(str,self.pane.text.tag_ranges('sel')))
        self.app.apply_font_sizes(15,18);self.pane.set_appearance(self.app.appearance)
        self.root.update_idletasks()
        self.assertEqual(self.pane.text.get('1.0','end'),before)
        self.assertEqual(tuple(map(str,self.pane.text.tag_ranges('sel'))),selection)
        self.assertIn(image,self.pane.text.image_names())
        self.assertEqual(self.pane.text.tag_cget('name','font'),str(self.app.fonts['username']))
        self.assertEqual(self.app.fonts['normal'].cget('size'),15)
        self.assertEqual(self.app.fonts['username'].cget('size'),18)
        self.app.change_font(1)
        self.assertEqual(core.font_sizes(self.app.appearance),(16,19))
        self.app.reset_font();self.assertEqual(core.font_sizes(self.app.appearance),(12,12))

    def test_font_settings_apply_and_shortcuts_keep_drafts(self):
        self.app.features.show_settings('Display')
        page=self.app.features.settings_ui.pages['Display'].inner
        spins=[child for row in page.winfo_children() for child in row.winfo_children() if isinstance(child,tk.Spinbox)]
        self.assertEqual(len(spins),4)
        self.app.change_font(1);self.assertEqual(spins[0].get(),'13');self.assertEqual(spins[1].get(),'13')
        spins[0].delete(0,'end');spins[0].insert(0,'16')
        self.app.change_font(1);self.assertEqual(spins[0].get(),'16');self.assertEqual(spins[1].get(),'14')
        next(w for w in page.winfo_children() if isinstance(w,tk.Button) and w.cget('text')=='Apply').invoke()
        self.assertEqual(core.font_sizes(self.app.appearance),(16,14))

    def test_alternate_rows_follow_messages_including_reply_context(self):
        self.pane.clear_messages(True)
        for i in range(1,5):self.pane.add(msg(str(i),'p','p',i),self.source)
        self.app.appearance['alternate_rows']=False;self.pane.set_appearance(self.app.appearance)
        self.app.appearance['alternate_rows']=True;self.pane.set_appearance(self.app.appearance)
        rows=list(self.pane.row_marks)
        for i,(mark,_) in enumerate(rows):
            self.assertEqual('alt' in self.pane.text.tag_names(mark),bool(i%2))
            self.assertEqual('alt' in self.pane.text.tag_names(str(mark)+'+1line'),bool(i%2))

    def test_thread_isolated_live_nested_dedup_and_composer_keeps_reply(self):
        thread,a,b,c=self.thread();pane=thread.pane
        self.assertEqual(len(pane.message_records),3)
        self.assertNotIn('message other',pane.text.get('1.0','end'))
        self.assertNotIn('Replying to',pane.text.get('1.0','end'))
        self.assertIn(pane,self.app.display_panes());self.assertNotIn(pane,self.app.all_panes())
        self.assertIs(self.app.features.chat_tools.show_thread(self.source,b),thread)
        self.publish(msg('d','c','a'));self.publish(msg('else','other','other'))
        self.drain(pane);self.assertEqual(len(pane.message_records),4)
        self.app.features.chat_tools.cancel_reply(pane)
        self.assertEqual(pane.reply_target[1]['id'],'a')
        self.assertFalse(pane.reply_bar.winfo_manager());self.assertFalse(pane.target_combo.winfo_manager())
        calls=[];owner=self.app.features
        with patch.object(owner,'twitch_identity',return_value={'user_id':'42'}),patch.object(owner,'broadcaster',return_value='12'),patch.object(owner,'helix',side_effect=lambda *a,**kw:calls.append(kw) or {'data':[{'is_sent':True,'message_id':'sent'}]}):
            owner.send(self.source,'test',pane.reply_target[1])
        self.assertEqual(calls[0]['body']['reply_parent_message_id'],'a')
        thread.close();self.assertNotIn(pane,self.source.views);self.assertNotIn(pane,self.app.extra_panes)
        self.assertFalse(self.app.features.chat_tools.threads)

    def test_missing_root_context_redaction_and_cap(self):
        self.app.cfg['message_limit']=100
        selected=self.publish(msg('b','a','a'))
        thread=self.app.features.chat_tools.show_thread(self.source,selected);self.drain(thread.pane)
        self.assertIn('context',thread.pane.text.get('1.0','end'))
        self.app.features.redact(self.source,{'message_id':'a'});self.drain(thread.pane)
        self.assertNotIn('context',thread.pane.text.get('1.0','end'))
        for i in range(120):self.publish(msg('x'+str(i),'a','a'))
        self.drain(thread.pane)
        self.assertLessEqual(len(thread.messages),100)
        self.assertLessEqual(len(thread.pane.message_records),100)
        self.assertIn('message x119',thread.pane.text.get('1.0','end'))

    def test_moderation_redacts_both_main_chat_and_thread(self):
        thread,a,b,c=self.thread();self.drain()
        self.app.features.redact(self.source,{'message_id':'b'});self.drain(thread.pane)
        self.assertNotIn('message b',self.pane.text.get('1.0','end'))
        self.assertNotIn('message b',thread.pane.text.get('1.0','end'))
        self.assertIn('[message deleted]',thread.pane.text.get('1.0','end'))

    def test_late_root_replaces_context_and_stays_first(self):
        selected=self.publish(msg('b','a','a'))
        thread=self.app.features.chat_tools.show_thread(self.source,selected)
        self.drain(thread.pane)
        self.publish(msg('a'))
        self.drain(thread.pane)
        contents=thread.pane.text.get('1.0','end')
        self.assertLess(contents.index('message a'),contents.index('message b'))
        self.assertNotIn('context',contents)
        self.assertEqual(len(thread.pane.message_records),2)

    def test_click_opens_thread_drag_selects_and_username_still_opens_card(self):
        thread,a,b,c=self.thread();thread.close();self.drain()
        self.root.deiconify();self.root.geometry('850x600');self.root.update()
        pane=self.pane;pane.finish_resize();self.root.update()
        spans=pane.text.tag_ranges('message_'+str(c['_seq']))
        index=str(spans[0]);bounds=pane.text.bbox(index)
        self.assertIsNotNone(bounds)
        x,y,w,h=bounds;event=SimpleNamespace(x=x+2,y=y+h//2)
        pane._left_click(event)
        pane._left_release(SimpleNamespace(x=event.x+25,y=event.y))
        self.assertFalse(self.app.features.chat_tools.threads)
        pane._left_click(event);pane._left_release(event)
        self.assertEqual(len(self.app.features.chat_tools.threads),1)
        with patch.object(self.app.features.chat_tools,'show_user') as show:
            name_range=pane.text.tag_nextrange('name',index,spans[-1]);bounds=pane.text.bbox(name_range[0])
            x,y,w,h=bounds
            self.assertEqual(pane._left_click(SimpleNamespace(x=x+2,y=y+h//2)),'break')
            show.assert_called_once()

    def test_thread_language_fonts_and_shutdown_cleanup(self):
        thread,*_=self.thread()
        self.app.apply_font_sizes(15,18)
        self.assertEqual(thread.pane.text.tag_cget('name','font'),str(self.app.fonts['username']))
        self.app.features.change_language('pt-BR');self.drain(thread.pane)
        self.assertIn('Conversa de respostas',thread.window.title())
        self.assertIn('Somente',thread.notice.cget('text'))
        self.app.features.close()
        self.assertFalse(self.app.extra_panes)
        set_language('en')


class PlacementTests(unittest.TestCase):
    def test_real_normal_restore_and_minimized_snapshot(self):
        root=tk.Tk();root.withdraw();root.geometry('620x460+120+100')
        manager=WindowState(root,{})
        root.deiconify();root.update();saved=manager.snapshot()
        root.destroy()
        root=tk.Tk();root.withdraw();root.geometry('400x300+0+0')
        manager=WindowState(root,{'window':saved})
        root.deiconify();root.update()
        try:
            restored=manager.snapshot()
            self.assertEqual(parse_geometry(restored['geometry'])[:2],(620,460))
            if sys.platform=='win32':self.assertEqual(restored['normal_rect'],saved['normal_rect'])
            root.iconify();root.update()
            self.assertEqual(manager.snapshot()['state'],'normal')
        finally:root.destroy()

    @unittest.skipUnless(sys.platform=='win32','Windows maximized placement')
    def test_maximized_restore_keeps_normal_bounds_when_minimized(self):
        root=tk.Tk();root.geometry('650x480+110+90');manager=WindowState(root,{})
        root.update();normal=manager.snapshot()['normal_rect']
        root.state('zoomed');root.update();saved=manager.snapshot()
        root.iconify();root.update();minimized=manager.snapshot();root.destroy()
        self.assertEqual(saved['state'],'zoomed');self.assertEqual(minimized['state'],'zoomed')
        self.assertEqual(minimized['normal_rect'],normal)
        root=tk.Tk();manager=WindowState(root,{'window':minimized});root.update()
        try:
            self.assertEqual(root.state(),'zoomed')
            root.state('normal');root.update()
            self.assertEqual(manager.snapshot()['normal_rect'],normal)
        finally:root.destroy()
