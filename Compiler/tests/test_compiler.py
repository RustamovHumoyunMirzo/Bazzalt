import copy,json,os,struct,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from Compiler.settings import DEFAULTS,ApplyPreset,Validate,Load,Save
from Compiler.pak import Pack,MAGIC,HEADER,SafeName
from Compiler.build import KeyFor,Publish,BuildLock

class CompilerTests(unittest.TestCase):
    def test_disposable_workspace_is_cleaned_on_failure_and_success(self):
        from Compiler.build import BuildWorkspace
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);scripts=root/'Scripts';scripts.mkdir();(scripts/'keep').touch()
            for fail in (False,True):
                try:
                    with BuildWorkspace(root) as work:
                        (work/'Cooked').mkdir();(work/'Cooked/data').write_bytes(b'x'*100)
                        if fail:raise ValueError('failed cook')
                except ValueError:pass
                self.assertFalse(work.exists());self.assertTrue((scripts/'keep').exists())
            for name in ('bad\x00name','bad\nname','a'*4097):
                with self.assertRaises(ValueError):SafeName(name)
    def test_status_protocol_is_safe_with_windows_legacy_codepages(self):
        import io
        from Compiler.build import Emit
        stream=io.BytesIO();text=io.TextIOWrapper(stream,encoding='cp1252')
        with patch('sys.stdout',text):Emit('complete','Done',output='C:/Документы/游戏/Game.exe')
        text.flush();event=json.loads(stream.getvalue().decode('ascii'));self.assertEqual(event['output'],'C:/Документы/游戏/Game.exe')
    def test_new_window_and_intro_settings_validate_and_upgrade(self):
        from Compiler.settings import Merge
        self.assertTrue(Merge({'version':1})['intro']['enabled'])
        for mode in ('windowed','maximized','borderless','fullscreen'):
            value=copy.deepcopy(DEFAULTS);value['window']['mode']=mode;self.assertEqual(Validate(value)['window']['mode'],mode)
        value=copy.deepcopy(DEFAULTS);value['intro']['duration_ms']=30001
        with self.assertRaises(ValueError):Validate(value)
    def test_settings_round_trip_and_presets(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'Game.bproject'
            for preset in ('low','medium','high','very_high'):
                value=ApplyPreset(DEFAULTS,preset);Save(project,value);self.assertEqual(Load(project),value)
            self.assertEqual(ApplyPreset(DEFAULTS,'very_high')['security']['key_storage'],'environment')
            self.assertNotIn('key',Load(project)['security'])
    def test_reject_unsupported_or_unsafe_settings(self):
        for section,key,value in (('production','name','../game'),('production','name','CON'),('target','architecture','arm'),('packaging','chunk_mb',0),('window','width',False),('window','resizable','yes'),('security','key_environment','PATH=secret')):
            settings=copy.deepcopy(DEFAULTS);settings[section][key]=value
            with self.assertRaises(ValueError):Validate(settings)
        settings=ApplyPreset(DEFAULTS,'medium');settings['packaging']['format']='loose'
        with self.assertRaises(ValueError):Validate(settings)
    def test_key_delivery_fails_closed(self):
        settings=ApplyPreset(DEFAULTS,'high')
        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'00'*32}):self.assertEqual(KeyFor(settings),bytes(32))
        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':''}):
            with self.assertRaises(ValueError):KeyFor(settings)
        self.assertEqual(len(KeyFor(ApplyPreset(DEFAULTS,'medium'))),32)
    def test_package_segments_are_authenticated_and_bounded(self):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        import hashlib
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'Data';source.mkdir();(source/'音声.txt').write_bytes(b'A'*(5*1024*1024));(source/'empty').touch();key=os.urandom(32)
            packs=Pack(source,root/'Pak',4,key);self.assertGreater(len(packs),1);restored={};nonces=set()
            for path in packs:
                with path.open('rb') as f:
                    self.assertEqual(f.read(8),MAGIC)
                    while header:=f.read(HEADER.size):
                        names,size,offset,total,encrypted=HEADER.unpack(header);self.assertLessEqual(size,4*1024*1024);name=f.read(names);nonce=f.read(12);tag=f.read(16);digest=f.read(32);data=f.read(size)
                        self.assertNotIn(nonce,nonces);nonces.add(nonce);plain=AESGCM(key).decrypt(nonce,data+tag,header+name);self.assertEqual(hashlib.sha256(plain).digest(),digest)
                        output=restored.setdefault(name,bytearray());self.assertEqual(len(output),offset);output.extend(plain)
            self.assertEqual(restored['音声.txt'.encode()],(source/'音声.txt').read_bytes());self.assertEqual(restored[b'empty'],b'')
    def test_paths_and_output_ownership(self):
        for name in ('../x','/x','C:/x','x\\y','x/../y','x/','x./y'):
            with self.assertRaises(ValueError):SafeName(name)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);staged=root/'new';staged.mkdir();existing=root/'bin';existing.mkdir();(existing/'user.txt').write_text('keep')
            with self.assertRaises(ValueError):Publish(staged,existing,'owner')
            self.assertEqual((existing/'user.txt').read_text(),'keep')
            with BuildLock(root):
                with self.assertRaises(ValueError):
                    with BuildLock(root):pass

if __name__=='__main__':unittest.main()
