import copy,json,os,struct,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from Compiler.settings import DEFAULTS,ApplyPreset,Validate,Load,Save
from Compiler.pak import Pack,MAGIC,HEADER,SafeName
from Compiler.build import KeyFor,Publish,BuildLock

class CompilerTests(unittest.TestCase):
    def test_cooker_validates_platform_architecture_and_execute_permission(self):
        from Compiler.build import ValidateCooker as InspectCooker
        def ValidateCooker(path,platform):return InspectCooker(path,platform,host_architecture='x64')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Cooker'
            with self.assertRaisesRegex(ValueError,'missing'):ValidateCooker(path,'linux')
            for architecture,machine in ((1,3),(2,62)):
                header=bytearray(20);header[:4]=b'\x7fELF';header[4]=architecture;header[5]=1;header[18:20]=machine.to_bytes(2,'little');path.write_bytes(header)
                with patch('Compiler.build.os.access',return_value=True):self.assertEqual(ValidateCooker(path,'linux'),path.resolve())
                with patch('Compiler.build.os.access',return_value=False),self.assertRaisesRegex(ValueError,'not executable'):ValidateCooker(path,'linux')
                with self.assertRaisesRegex(ValueError,'platform mismatch'):ValidateCooker(path,'windows')
            header[18:20]=(0xdead).to_bytes(2,'little');path.write_bytes(header)
            with self.assertRaisesRegex(ValueError,'Unsupported ELF architecture'):ValidateCooker(path,'linux')
            path.write_bytes(b'MZ'+bytes(100))
            with self.assertRaisesRegex(ValueError,'platform mismatch'):ValidateCooker(path,'linux')

    def test_explicit_key_generator_requires_no_project(self):
        import subprocess,sys
        values=[]
        for _ in range(2):
            result=subprocess.run([sys.executable,'-m','Compiler.build','--generate-key'],capture_output=True,text=True,check=True)
            value=result.stdout.strip();self.assertEqual(len(value),64);self.assertEqual(len(bytes.fromhex(value)),32);values.append(value)
        self.assertNotEqual(*values)
    def test_package_cancellation_closes_partial_output(self):
        from Compiler.build import BuildCancelled
        calls=0
        def check():
            nonlocal calls
            calls+=1
            if calls==3:raise BuildCancelled('cancel')
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'Data';source.mkdir();(source/'a.bin').write_bytes(os.urandom(5*1024*1024))
            with self.assertRaises(BuildCancelled):Pack(source,root/'Pak',check=check)
            for path in (root/'Pak').glob('*'):path.unlink()
    def test_key_separation_and_encrypted_manifest_envelopes(self):
        from Compiler.security import SecurityContext,Derive
        from Compiler.integrity import Seal
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        settings=ApplyPreset(DEFAULTS,'high');master=bytes(range(32))
        first=SecurityContext(settings,master);second=SecurityContext(settings,master)
        self.assertNotEqual(first.Root,master);self.assertNotEqual(first.PackageKey,second.PackageKey)
        self.assertNotEqual(Derive(first.Root,first.Salt,'inventory'),Derive(first.Root,first.Salt,'configuration'))
        self.assertNotEqual(Derive(first.Root,first.Salt,'configuration'),first.PackageKey)
        blob=first.ProtectManifest(b'{"private":"asset names"}','inventory');self.assertNotIn(b'asset names',blob)
        key=Derive(first.Root,first.Salt,'inventory');self.assertEqual(AESGCM(key).decrypt(blob[8:20],blob[20:],first.BuildId+b'inventory'),b'{"private":"asset names"}')
        changed=bytearray(blob);changed[-1]^=1
        from cryptography.exceptions import InvalidTag
        with self.assertRaises(InvalidTag):AESGCM(key).decrypt(bytes(changed[8:20]),bytes(changed[20:]),first.BuildId+b'inventory')
        with self.assertRaises(InvalidTag):AESGCM(key).decrypt(blob[8:20],blob[20:],second.BuildId+b'inventory')
        old=copy.deepcopy(DEFAULTS);old['security']['encryption']='aes-256-gcm';del old['security']['encrypt_manifests']
        self.assertTrue(Validate(old)['security']['encrypt_manifests'])
    def test_disposable_workspace_is_cleaned_on_failure_and_success(self):
        from Compiler.build import BuildWorkspace
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);scripts=root/'Scripts';scripts.mkdir();(scripts/'keep').touch()
            stale=root/'work-abandoned';stale.mkdir();(stale/'.bazzalt-build-workspace').write_bytes(b'BazzaltBuildWorkspace-v1')
            unowned=root/'work-user';unowned.mkdir();(unowned/'keep').touch()
            for fail in (False,True):
                try:
                    with BuildWorkspace(root) as work:
                        (work/'Cooked').mkdir();(work/'Cooked/data').write_bytes(b'x'*100)
                        if fail:raise ValueError('failed cook')
                except ValueError:pass
                self.assertFalse(work.exists());self.assertTrue((scripts/'keep').exists())
            self.assertFalse(stale.exists());self.assertTrue((unowned/'keep').exists())
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
        from Compiler.settings import Merge
        for invalid in ({'security':'high'},{'security':{'unknown_protection':True}},{'version':True}):
            with self.assertRaises(ValueError):Merge(invalid)
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
        import hashlib,hmac
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'Data';source.mkdir();(source/'音声.txt').write_bytes(os.urandom(5*1024*1024));(source/'empty').touch();key=os.urandom(32)
            packs=Pack(source,root/'Pak',4,key);self.assertGreater(len(packs),1);restored={};nonces=set()
            self.assertFalse(any('音声.txt'.encode() in path.read_bytes() for path in packs))
            for path in packs:
                with path.open('rb') as f:
                    self.assertEqual(f.read(8),MAGIC)
                    while header:=f.read(HEADER.size):
                        names,size,raw_size,offset,total,flags=HEADER.unpack(header);self.assertLessEqual(size,4*1024*1024);self.assertEqual(flags,1);self.assertEqual(raw_size,size);name=f.read(names);nonce=f.read(12);tag=f.read(16);digest=f.read(32);data=f.read(size)
                        encoded_name=name;self.assertNotIn(encoded_name[:12],nonces);nonces.add(encoded_name[:12]);name=AESGCM(key).decrypt(encoded_name[:12],encoded_name[12:],header)
                        self.assertNotIn(nonce,nonces);nonces.add(nonce);plain=AESGCM(key).decrypt(nonce,data+tag,header+encoded_name);self.assertEqual(hmac.digest(hmac.digest(key,b'Bazzalt/v3/segment-digest','sha256'),plain,'sha256'),digest)
                        output=restored.setdefault(name,bytearray());self.assertEqual(len(output),offset);output.extend(plain)
            self.assertEqual(restored['音声.txt'.encode()],(source/'音声.txt').read_bytes());self.assertEqual(restored[b'empty'],b'')
    def test_compression_and_duplicate_records_reduce_package_size(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'Data';source.mkdir();data=b'Compress and deduplicate me!\n'*100000
            (source/'a.bin').write_bytes(data);(source/'b.bin').write_bytes(data)
            packs=Pack(source,root/'Pak');self.assertEqual(len(packs),1)
            if os.name=='nt':self.assertLess(packs[0].stat().st_size,len(data)//10)
            else:self.assertLess(packs[0].stat().st_size,len(data)+4096)
            flags=[]
            with packs[0].open('rb') as file:
                file.read(8)
                while header:=file.read(HEADER.size):
                    names,size,raw_size,offset,total,bits=HEADER.unpack(header);flags.append(bits);file.seek(names+60+size,1)
            self.assertEqual(flags[-1],4)
            if os.name=='nt':self.assertEqual(flags[0],2)
    def test_signed_inventory_is_pinned_without_shipping_private_key(self):
        from Compiler.integrity import Seal,SignatureInput
        from cryptography.hazmat.primitives.asymmetric import ec,utils
        from cryptography.hazmat.primitives import hashes
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);player=root/'Game.exe';pe=bytearray(512);pe[:2]=b'MZ';struct.pack_into('<I',pe,60,64);pe[64:68]=b'PE\0\0';struct.pack_into('<H',pe,88,0x20b);player.write_bytes(pe);(root/'game.json').write_text('{}')
            Seal(root,player);seal=player.read_bytes()[-112:];self.assertEqual(seal[:8],b'BZSIG003')
            public=ec.EllipticCurvePublicNumbers(int.from_bytes(seal[8:40],'big'),int.from_bytes(seal[40:72],'big'),ec.SECP256R1()).public_key()
            signature=(root/'game.signature').read_bytes();der=utils.encode_dss_signature(int.from_bytes(signature[:32],'big'),int.from_bytes(signature[32:],'big'))
            public.verify(der,SignatureInput((root/'game.bootstrap').read_bytes(),(root/'game.integrity').read_bytes()),ec.ECDSA(hashes.SHA256()))
            self.assertEqual([v['path'] for v in json.loads((root/'game.integrity').read_bytes())['files']],['game.json'])
            self.assertFalse(any('private' in p.name for p in root.iterdir()))
    def test_asset_root_cannot_recursively_include_build_outputs(self):
        from Compiler.build import CompileScripts
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);project=root/'Game.bproject';project.write_text('AssetDirectory: .\n')
            with self.assertRaisesRegex(ValueError,'dedicated folder'):CompileScripts(project,root/'Runtime','x64',root/'Cache')
    def test_paths_and_output_ownership(self):
        for name in ('../x','/x','C:/x','x\\y','x/../y','x/','x./y','COM¹.txt','CONIN$','LPT0'):
            with self.assertRaises(ValueError):SafeName(name)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);staged=root/'new';staged.mkdir();existing=root/'bin';existing.mkdir();(existing/'user.txt').write_text('keep')
            with self.assertRaises(ValueError):Publish(staged,existing,'owner')
            self.assertEqual((existing/'user.txt').read_text(),'keep')
            with BuildLock(root):
                with self.assertRaises(ValueError):
                    with BuildLock(root):pass

if __name__=='__main__':unittest.main()
