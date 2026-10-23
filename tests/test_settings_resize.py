import tkinter as tk
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import test_chat as _base
core = _base.core
from ytchatterino_i18n import set_language, tr
from ytchatterino_settings import settings_matches, APPEARANCE_PAGES, SECTIONS


class SettingsSearchTests(unittest.TestCase):
    def tearDown(self):
        set_language('en')

    def test_control_names_and_keywords(self):
        self.assertIn(('Display','Emote height (px)'),settings_matches('emote size'))
        self.assertIn(('Messages','Default message limit per pane'),settings_matches('2000'))
        self.assertIn(('Accounts','Choose Google OAuth JSON…'),settings_matches('oauth json'))
        self.assertEqual(settings_matches(''),[])

    def test_portuguese_and_accents(self):
        set_language('pt-BR')
        self.assertIn(('Display','Emote height (px)'),settings_matches('tamanho emote'))
        self.assertIn(('Messages','Default message limit per pane'),settings_matches('memória'))
        self.assertTrue(any(page=='Highlights' for page,_ in settings_matches('destaques')))
        self.assertEqual(str(tr('Search settings')),'Pesquisar configurações')


class SettingsUiTests(unittest.TestCase):
    setUp=_base.UiTests.setUp
    tearDown=_base.UiTests.tearDown
    msg=_base.UiTests.msg

    def test_appearance_grouping_and_cached_drafts(self):
        owner=self.app.features
        owner.show_settings('Highlights')
        ui=owner.settings_ui
        self.assertEqual(ui.current,'Highlights')
        self.assertNotIn('Messages',ui.buttons)
        self.assertNotIn('Highlights',ui.buttons)
        self.assertEqual(set(ui.appearance_buttons),set(APPEARANCE_PAGES))
        self.assertEqual(set(ui.buttons),set(SECTIONS))
        old=ui.pages['Highlights']
        users=next(child for child in old.inner.winfo_children() if isinstance(child,tk.Text))
        users.insert('end','draft_user #227400')
        owner.select_settings('Messages');owner.select_settings('Highlights')
        self.assertIs(ui.pages['Highlights'],old)
        self.assertIn('draft_user',users.get('1.0','end'))

    def test_search_navigation_no_results_and_clear(self):
        owner=self.app.features;owner.show_settings('Appearance');ui=owner.settings_ui
        ui.query.set('emote size');ui.search()
        self.assertTrue(ui.search_results)
        ui.choose_result()
        self.assertEqual(ui.current,'Display')
        ui.query.set('nothingwillmatchxyz');ui.search()
        self.assertFalse(ui.search_results)
        self.assertIn('No settings found',ui.result_count.cget('text'))
        ui.query.set('');ui.search()
        self.assertFalse(ui.search_results)
        self.assertEqual(ui.current,'Display')

    def test_language_change_rebuilds_grouped_page(self):
        owner=self.app.features;owner.show_settings('Messages');ui=owner.settings_ui
        first=ui.pages['Messages']
        owner.change_language('pt-BR')
        self.assertEqual(ui.current,'Messages')
        self.assertIsNot(ui.pages['Messages'],first)
        self.assertEqual(ui.lang,'pt-BR')
        self.assertEqual(ui.buttons['Appearance'].cget('text'),'Aparência')

    def fill(self,count=150):
        self.pane.clear_messages(True)
        for i in range(1,count+1):self.pane.add(self.msg(i,'Text Kappa '*(i%4+1)),self.source)
        self.pane._viewport_configure(SimpleNamespace(width=600,height=500))
        self.pane.finish_resize()

    def test_resize_preserves_history_tags_images_and_selection(self):
        self.fill()
        photo=tk.PhotoImage(master=self.root,width=4,height=4)
        self.pane.text.configure(state='normal')
        name=self.pane.text.image_create('140.2',image=photo)
        self.pane.text.tag_add('sel','130.0','140.1')
        self.pane.text.configure(state='disabled')
        before=self.pane.text.get('1.0','end')
        selection=tuple(str(x) for x in self.pane.text.tag_ranges('sel'))
        self.pane._viewport_configure(SimpleNamespace(width=420,height=500))
        self.assertTrue(self.pane.resizing)
        self.assertIsNotNone(self.pane.resize_surface)
        self.pane.finish_resize()
        self.assertFalse(self.pane.resizing)
        self.assertEqual(self.pane.text.get('1.0','end'),before)
        self.assertEqual(tuple(str(x) for x in self.pane.text.tag_ranges('sel')),selection)
        self.assertIn(name,self.pane.text.image_names())
        self.assertEqual(len(self.pane.message_records),150)
        self.assertFalse(self.pane.resize_surface.get('1.0','end').strip())

    def test_incoming_messages_wait_for_resize(self):
        self.fill()
        self.pane._viewport_configure(SimpleNamespace(width=400,height=500))
        event=(1000,'msg',self.msg(1000,'after resize'),self.source)
        self.pane.pending_events.append(event)
        self.pane.drain(time.monotonic()+1)
        self.assertTrue(self.pane.pending_events)
        self.pane.finish_resize();self.pane.drain(time.monotonic()+1)
        self.assertIn('after resize',self.pane.text.get('1.0','end'))

    def test_resize_timer_coalesces_and_latest_size_wins(self):
        self.fill()
        for width in (440,460,480):self.pane._viewport_configure(SimpleNamespace(width=width,height=510))
        self.assertIsNotNone(self.pane.viewport_after_id)
        self.pane.finish_resize()
        self.assertEqual(self.pane.viewport_applied,(480,510))
        self.assertIsNone(self.pane.resize_after_id)
        self.assertIsNone(self.pane.viewport_after_id)

    def test_resize_keeps_following_latest_after_layout(self):
        self.root.deiconify()
        self.root.geometry('900x600')
        self.root.update()
        self.fill(180)
        self.pane.jump_latest()
        self.root.update()
        self.root.geometry('440x600')
        self.root.update()
        self.pane.finish_resize()
        self.root.update()
        self.assertTrue(self.pane.follow_tail)
        self.assertGreaterEqual(self.pane.text.yview()[1], .995)

    def test_resize_preserves_scrolled_message(self):
        self.root.deiconify()
        self.root.geometry('900x600')
        self.root.update()
        self.fill(180)
        self.root.update()
        self.pane.text.yview('90.0')
        self.pane._remember_scroll()
        self.root.update()
        anchor = self.pane.text.index('@0,0').split('.')[0]
        self.root.geometry('440x600')
        self.root.update()
        self.pane.finish_resize()
        self.root.update()
        self.assertFalse(self.pane.follow_tail)
        self.assertEqual(self.pane.text.index('@0,0').split('.')[0], anchor)


if __name__=='__main__':unittest.main()
