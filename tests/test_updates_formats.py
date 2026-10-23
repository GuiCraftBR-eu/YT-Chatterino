from pathlib import Path
from datetime import datetime
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch, Mock
import zipfile
import test_chat as base
from ytchatterino_i18n import display_time, display_date, set_datetime_formats, set_language
from ytchatterino_settings import settings_matches
import ytchatterino_update as updates

core = base.core


def release(kind='source', version='1.2.3'):
    name = ('YT-Chatterino-Windows11-' + version + '.zip') if kind == 'windows' else (
        'yt-chatterino-' + version + '-1-any.pkg.tar.zst' if kind == 'pacman' else
        'YT-Chatterino-ArchLinux-' + version + '.tar.gz')
    return {'tag_name':'v'+version, 'published_at':'2026-10-23T10:20:30Z', 'assets':[
        {'name':name, 'browser_download_url':updates.DOWNLOAD_PREFIX+'v'+version+'/'+name,
         'digest':'sha256:'+'a'*64, 'size':100}]}


class FormatTests(unittest.TestCase):
    def tearDown(self):
        set_datetime_formats({});set_language('en')

    def test_default_date_and_all_formats(self):
        set_datetime_formats({})
        self.assertEqual(display_date('2026-01-09T00:00:00Z'),'09/01/2026')
        for fmt, expected in [('MM/DD/YYYY','01/09/2026'),('YYYY-MM-DD','2026-01-09'),('DD.MM.YYYY','09.01.2026')]:
            set_datetime_formats({'date_format':fmt})
            self.assertEqual(display_date('2026-01-09'),expected)

    def test_seconds_cycles_midnight_and_noon(self):
        set_datetime_formats({'time_format':'HH:mm:ss'})
        self.assertEqual(display_time('1:02:03 pm'),'13:02:03')
        self.assertEqual(display_time('12:00 am'),'00:00:00')
        set_datetime_formats({'time_format':'hh:mm:ss AM/PM'})
        self.assertEqual(display_time('00:02:04'),'12:02:04 AM')
        self.assertEqual(display_time('12:02:04'),'12:02:04 PM')
        set_datetime_formats({'time_format':'hh:mm AM/PM'})
        self.assertEqual(display_time('21:04:05'),'9:04 PM')

    def test_epoch_takes_priority_and_language_is_independent(self):
        stamp=datetime(2026,1,9,13,2,47).timestamp()
        set_datetime_formats({'time_format':'HH:mm:ss'})
        self.assertEqual(display_time('1:02 pm',stamp),'13:02:47')
        set_language('pt-BR')
        self.assertEqual(display_time('1:02 pm',stamp),'13:02:47')

    def test_invalid_preferences_and_times(self):
        set_datetime_formats({'date_format':[], 'time_format':'nonsense'})
        self.assertEqual(display_date('2026-01-09'),'09/01/2026')
        self.assertEqual(display_time('13:04'),'13:04')
        self.assertEqual(display_time('99:99'),'99:99')
        self.assertEqual(display_date('unknown'),'unknown')

    def test_new_settings_search_in_both_languages(self):
        set_language('en')
        self.assertIn(('Display','Timestamp format'),settings_matches('segundos'))
        self.assertIn(('Display','Date format'),settings_matches('date format'))
        self.assertTrue(any(page=='Highlights' for page,_ in settings_matches('destaques')))
        self.assertIn(('Updates','Check for updates automatically'),settings_matches('automatic update'))


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='yt-update-tests-')
        self.root=Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_version_order_stable_only_and_asset_platform(self):
        self.assertGreater(updates.version_tuple('v1.2.10'),updates.version_tuple('1.2.2'))
        for kind in ('source','windows','pacman'):
            self.assertEqual(updates.release_info(release(kind),'1.2.2',kind)['kind'],kind)
        self.assertIsNone(updates.release_info(release(),'1.2.3','source'))
        self.assertIsNone(updates.release_info(dict(release(),prerelease=True),'1.2.2','source'))
        with self.assertRaises(ValueError):updates.version_tuple('1.2.3-rc1')

    def test_wrong_host_missing_digest_and_size_are_rejected(self):
        for key, value in [('browser_download_url','https://example.com/evil.tar.gz'),('digest',None),('size',updates.MAX_DOWNLOAD+1)]:
            data=release();data['assets'][0][key]=value
            with self.assertRaises(ValueError):updates.release_info(data,'1.2.2','source')
        with self.assertRaises(ValueError):updates.release_info(dict(release(),assets=[]),'1.2.2','source')

    def test_download_checks_size_and_sha256(self):
        data=b'an update';info=updates.release_info(release(),'1.2.2','source')
        info.update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
        with patch('urllib.request.urlopen',return_value=io.BytesIO(data)):
            updates.download_asset(info,self.root/'download')
        self.assertEqual((self.root/'download').read_bytes(),data)
        info['sha256']='a'*64
        with patch('urllib.request.urlopen',return_value=io.BytesIO(data)), self.assertRaises(ValueError):
            updates.download_asset(info,self.root/'bad')

    def test_zip_traversal_links_duplicates_and_tar_links(self):
        for name in ('../escape','/absolute','C:/drive','folder\\escape'):
            archive=self.root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as z:
                info=zipfile.ZipInfo('placeholder');info.filename=name;z.writestr(info,b'bad')
            with self.assertRaises(ValueError):updates.extract_archive(archive,self.root/'extracted')
        with zipfile.ZipFile(self.root/'link.zip','w') as z:
            info=zipfile.ZipInfo('link');info.create_system=3;info.external_attr=0o120777<<16;z.writestr(info,b'outside')
        with self.assertRaises(ValueError):updates.extract_archive(self.root/'link.zip',self.root/'links')
        with tarfile.open(self.root/'link.tar.gz','w:gz') as t:
            info=tarfile.TarInfo('link');info.type=tarfile.SYMTYPE;info.linkname='../outside';t.addfile(info)
        with self.assertRaises(ValueError):updates.extract_archive(self.root/'link.tar.gz',self.root/'links')
        with zipfile.ZipFile(self.root/'dup.zip','w') as z:z.writestr('A',b'a');z.writestr('a',b'b')
        with self.assertRaises(ValueError):updates.extract_archive(self.root/'dup.zip',self.root/'duplicates')

    def stage(self, kind='source'):
        target=self.root/'app';target.mkdir()
        (target/'ytchatterino.pyw').write_text('APP_VERSION = "1.2.2"\n')
        (target/'google-oauth-client.json').write_text('private fixture')
        if kind=='windows':
            (target/'YT-Chatterino.exe').write_bytes(b'old exe')
            (target/'update-manifest.json').write_text(json.dumps({'files':['YT-Chatterino.exe','update-manifest.json']}))
        container=self.root/('.ytchatterino-update-'+kind);container.mkdir()
        stage=container/'stage';stage.mkdir()
        source=stage
        if kind=='windows':
            (stage/'YT-Chatterino.exe').write_bytes(b'new exe')
            source=stage/'Source';source.mkdir()
        (source/'ytchatterino.pyw').write_text('APP_VERSION = "1.2.3"\n')
        (source/'new_module.py').write_text('new')
        return {'kind':kind,'target':str(target),'stage':str(stage),'container':str(container),
                'backup':str(self.root/('.ytchatterino-backup-'+kind)),'version':'1.2.3','parent_pid':123456,'python':sys.executable}

    def test_source_install_preserves_private_files_and_can_rollback(self):
        manifest=self.stage()
        restore=updates.apply_files(manifest)
        target=Path(manifest['target'])
        self.assertEqual(updates.source_version(target),'1.2.3')
        self.assertEqual((target/'google-oauth-client.json').read_text(),'private fixture')
        restore()
        self.assertEqual(updates.source_version(target),'1.2.2')
        self.assertFalse((target/'new_module.py').exists())

    def test_windows_swap_preserves_private_files_and_can_rollback(self):
        manifest=self.stage('windows')
        restore=updates.apply_files(manifest);target=Path(manifest['target'])
        self.assertEqual((target/'YT-Chatterino.exe').read_bytes(),b'new exe')
        self.assertEqual((target/'google-oauth-client.json').read_text(),'private fixture')
        restore()
        self.assertEqual((target/'YT-Chatterino.exe').read_bytes(),b'old exe')

    def test_partial_source_failure_restores_old_files(self):
        manifest=self.stage();actual=os.replace
        def fail_new(src,dst):
            if Path(src).name=='new_module.py':raise OSError('simulated locked file')
            return actual(src,dst)
        with patch('ytchatterino_update.os.replace',side_effect=fail_new),self.assertRaises(OSError):
            updates.apply_files(manifest)
        self.assertEqual(updates.source_version(manifest['target']),'1.2.2')

    def test_invalid_install_paths_rejected_without_changes(self):
        manifest=self.stage();manifest['backup']=str(self.root.parent/'elsewhere')
        with self.assertRaises(ValueError):updates.apply_files(manifest)
        self.assertEqual(updates.source_version(manifest['target']),'1.2.2')

    def test_prepare_checks_archive_version_and_complete_sources(self):
        source=self.root/'payload'/'YT-Chatterino-1.2.3'/'src';source.mkdir(parents=True)
        for name in ('ytchatterino.pyw','ytchatterino_features.py','ytchatterino_chat.py',
                     'ytchatterino_update.py','ytchatterino_i18n.py','ytchatterino_settings.py',
                     'ytchatterino_youtube.py','ytchatterino_translations.py','ytchatterino_chat_translations.py'):
            (source/name).write_text('APP_VERSION = "1.2.3"\n')
        archive=self.root/'payload.tar.gz'
        with tarfile.open(archive,'w:gz') as opened:opened.add(source.parent,arcname=source.parent.name)
        target=self.root/'installed';target.mkdir();(target/'ytchatterino.pyw').write_text('APP_VERSION = "1.2.2"\n')
        info=updates.release_info(release(),'1.2.2','source')
        def download(info,dest,progress):shutil.copy2(archive,dest)
        manifest_path=updates.prepare_update(info,target,downloader=download)
        data=json.loads(manifest_path.read_text())
        self.assertEqual(updates.source_version(data['stage']),'1.2.3')
        command=updates.helper_command(manifest_path)
        self.assertEqual(command[-2:],['--apply-update',str(manifest_path)])
        self.assertTrue(Path(command[1]).is_file())

    def test_helper_startup_failure_rolls_back_before_relaunch(self):
        manifest=self.stage();path=Path(manifest['container'])/'update.json';path.write_text(json.dumps(manifest))
        with patch.object(updates,'wait_for_process'),patch.object(updates.subprocess,'Popen',return_value=SimpleNamespace(poll=lambda:1)) as spawned,patch('tkinter.messagebox.showerror'):
            self.assertEqual(updates.run_helper(path),1)
        self.assertEqual(updates.source_version(manifest['target']),'1.2.2')
        self.assertEqual(spawned.call_count,2)

    def test_helper_success_requires_startup_acknowledgement(self):
        manifest=self.stage();container=Path(manifest['container']);path=container/'update.json';path.write_text(json.dumps(manifest))
        def start(*args,**kwargs):
            (container/'ready.json').write_text('{}')
            return SimpleNamespace(poll=lambda:None)
        with patch.object(updates,'wait_for_process'),patch.object(updates.subprocess,'Popen',side_effect=start):
            self.assertEqual(updates.run_helper(path),0)
        self.assertEqual(updates.source_version(manifest['target']),'1.2.3')
        self.assertTrue(json.loads((container/'done.json').read_text())['success'])


class FormatUiTests(unittest.TestCase):
    setUp=base.UiTests.setUp
    tearDown=base.UiTests.tearDown
    msg=base.UiTests.msg

    def test_reformat_existing_messages_and_hidden_timestamps(self):
        message=self.msg(1);message['sent_at']=datetime(2026,1,9,13,2,47).timestamp()
        self.pane.add(message,self.source)
        set_datetime_formats({'time_format':'HH:mm:ss'})
        self.pane.refresh_timestamps()
        self.assertIn('13:02:47',self.pane.text.get('1.0','end'))
        self.assertEqual(len(self.pane.message_records),1)
        appearance=dict(self.app.appearance,show_timestamps=False)
        self.pane.set_appearance(appearance)
        self.assertEqual(str(self.pane.text.tag_cget('time','elide')),'1')
        set_datetime_formats({'time_format':'hh:mm AM/PM'});self.pane.refresh_timestamps()
        appearance['show_timestamps']=True;self.pane.set_appearance(appearance)
        self.assertIn('1:02 PM',self.pane.text.get('1.0','end'))
        self.assertTrue(self.pane.text.tag_ranges('time'))

    def test_quota_visible_only_for_focused_youtube_pane(self):
        self.app.update_quota();self.assertEqual(self.app.quota_lbl.winfo_manager(),'')
        with patch.object(core.ChatSource,'ensure_worker'):
            youtube=self.app.source_for({'platform':'youtube','id':'12345678901'})
            work=self.app.new_workspace([youtube])
        self.app.select(work)
        self.assertEqual(self.app.quota_lbl.winfo_manager(),'pack')
        self.app.select(self.workspace)
        self.assertEqual(self.app.quota_lbl.winfo_manager(),'')
        self.app.close_tab(work)
        self.assertEqual(self.app.quota_lbl.winfo_manager(),'')

    def test_update_arrow_confirmation_no_update_until_clicked(self):
        updater=self.app.updater
        info=updates.release_info(release(),'1.2.2','source')
        updater.checked(info)
        self.assertEqual(self.app.update_button.winfo_manager(),'pack')
        with patch.object(updater,'install') as install:
            updater.show()
            self.assertEqual(str(updater.install_button.cget('state')),'normal')
            install.assert_not_called()
            updater.window.destroy()
        updater.checked(None)
        self.assertEqual(self.app.update_button.winfo_manager(),'')

    def test_emote_pattern_and_filters_and_highlights(self):
        pattern=core.compile_emote_pattern(('Kappa','LUL','cat+'))
        self.assertEqual([m.group() for m in pattern.finditer('Kappa xKappa LUL cat+')],['Kappa','LUL','cat+'])
        self.source.handle_event('emote_catalog',(2,{'Kappa':{}},pattern))
        self.source.handle_event('emote_catalog',(1,{'stale':{}},None))
        self.assertIn('Kappa',self.source.emote_map)
        self.pane.set_filters({'hidden_users':['hidden'],'include_terms':['hello'],'exclude_terms':['bad']})
        self.assertTrue(self.pane.accepts({'name':'Tester','text':'hello'}))
        self.assertFalse(self.pane.accepts({'name':'HIDDEN','text':'hello'}))
        self.assertFalse(self.pane.accepts({'name':'Tester','text':'hello bad'}))
        self.app.features.settings['highlight']['users']={'tester':'#227400'}
        self.pane.add(self.msg(1,'hello'),self.source)
        self.assertTrue(self.pane.text.tag_ranges('__highlight_227400'))


if __name__=='__main__':unittest.main()
