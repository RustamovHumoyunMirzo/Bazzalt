"""Run the production APK player on an authorized, isolated Android test device."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile
from xml.etree import ElementTree

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from Compiler.android_tools import Validate
from Compiler.android_build import Launch
from Compiler.build import Build, Run
from Compiler.settings import DEFAULTS, Merge, ApplyPreset, Save
from Editor.scripting import ScriptAttachments, ScriptCompiler

PROJECT_ID='24371a94-735c-46d9-a022-4bb9932f6b55'
ENTITY_ID='34371a94-735c-46d9-a022-4bb9932f6b55'
PACKAGE='com.bazzalt.integration'


def Test(runtime,cooker):
    from PIL import Image
    import yaml
    root=ROOT/'dist/android-integration'
    if root.exists():raise ValueError('Android integration workspace must be clean')
    assets=root/'Assets';assets.mkdir(parents=True)
    project=root/'Game.bproject'
    project.write_text(yaml.safe_dump(dict(FormatVersion=1,ProjectUUID=PROJECT_ID,Name='Android Integration',
        AssetDirectory='Assets',StartupScene='Assets/Main.bscene',Properties={})),encoding='utf-8')
    scene=dict(FormatVersion=1,SceneUUID='44371a94-735c-46d9-a022-4bb9932f6b55',Entities=[
        dict(UUID=ENTITY_ID,Parent='00000000-0000-0000-0000-000000000000',Name='Probe',Components=[]),
        dict(UUID='54371a94-735c-46d9-a022-4bb9932f6b55',Parent='00000000-0000-0000-0000-000000000000',Name='Camera',
             Components=[dict(Type='Bazzalt.Camera',Version=1,Properties={
                 'Projection':'0','FieldOfView':'1.0471975512','OrthographicSize':'10',
                 'Near':'.1','Far':'1000','Aspect':'1.7777778','Priority':'0','Active':'true',
                 'Clear.R':'0','Clear.G':'0','Clear.B':'0','Clear.A':'1','Post.Enabled':'false',
                 'Post.Bloom':'false','Post.AO':'false','Post.AA':'0','Post.ToneMapping':'0','Post.Exposure':'0'})])])
    (assets/'Main.bscene').write_text(yaml.safe_dump(scene),encoding='utf-8')
    source=assets/'Probe.cpp'
    shutil.copy2(ROOT/'Compiler/tests/fixtures/AndroidIntegrationProbe.cpp',source)
    attachments=ScriptAttachments(root);attachments.Attach(ENTITY_ID,ScriptCompiler.Inspect(source))
    settings=Merge(copy.deepcopy(DEFAULTS));settings['target']['platform']='android'
    settings=ApplyPreset(settings,'medium');profile=settings['platforms']['android']
    profile.update(architecture='x64',application_id=PACKAGE,runtime_directory=str(runtime))
    settings['intro']['enabled']=False
    tools=Validate(profile,'x64',runtime)
    adb=tools.Sdk/'platform-tools/adb'
    if PACKAGE in Run([adb,'shell','pm','list','packages',PACKAGE]).stdout:Run([adb,'uninstall',PACKAGE])
    marker='files/'+PROJECT_ID+'/Player/Saves/profile_default/smoke.txt'
    try:
        for ordinal,(backend,delivery) in enumerate((('opengles','apk'),('vulkan','split_apk'),('opengles','obb'),('opengles','external_data')),1):
            profile.update(backend=backend,delivery=delivery)
            attachments.values['entities'][ENTITY_ID][0]['properties']['ExpectedRuns']=ordinal;attachments.Save()
            Save(project,settings)
            output=Build(project,runtime,cooker,configuration='debug')
            with zipfile.ZipFile(output/'base.apk') as apk:inventory=json.loads(apk.read('assets/delivery.json'))
            Launch(tools,profile,output,inventory)
            deadline=time.monotonic()+120
            result=''
            while time.monotonic()<deadline:
                process=subprocess.run([str(adb),'shell','run-as',PACKAGE,'cat',marker],capture_output=True,text=True,timeout=15)
                result=process.stdout.strip()
                if result=='PASS '+str(ordinal)+' Android':break
                time.sleep(1)
            else:raise RuntimeError('Android '+backend+'/'+delivery+' did not finish: '+result)
            screenshot=root/(backend+'-'+delivery+'.png')
            pixels=subprocess.run([str(adb),'exec-out','screencap','-p'],capture_output=True,check=True,timeout=30).stdout
            screenshot.write_bytes(pixels)
            with Image.open(screenshot) as image:
                width,height=image.size
                image=image.convert('RGB').crop((width//4,height//4,3*width//4,3*height//4)).resize((128,128))
                green=sum(1 for red,g,b in image.getdata() if g>40 and g>red*1.5 and g>b*1.2)
                if green<10:raise RuntimeError('Android '+backend+' did not present the green physics object')
            prefs=Run([adb,'shell','run-as',PACKAGE,'cat','shared_prefs/Bazzalt.SaveGame.xml']).stdout
            entries=ElementTree.fromstring(prefs).findall('string')
            if not entries or any('/slot_' not in entry.attrib['name'] for entry in entries):
                raise RuntimeError('Unexpected Android save preference keys')
            keys=Run([adb,'shell','run-as',PACKAGE,'ls','no_backup/keys']).stdout
            if '.key' not in keys:raise RuntimeError('Encrypted content did not provision a Keystore-protected key file')
            Run([adb,'shell','input','keyevent','KEYCODE_BACK'])
            deadline=time.monotonic()+20
            shutdown=marker.replace('smoke.txt','shutdown.txt')
            while time.monotonic()<deadline:
                process=subprocess.run([str(adb),'shell','run-as',PACKAGE,'cat',shutdown],capture_output=True,text=True,timeout=15)
                if process.stdout.strip()=='SHUTDOWN '+str(ordinal):break
                time.sleep(.5)
            else:raise RuntimeError('Android game did not shut down its script lifecycle')
            print('Android runtime passed',backend,delivery,flush=True)
    finally:
        pixels=subprocess.run([str(adb),'exec-out','screencap','-p'],capture_output=True,timeout=30).stdout
        if pixels:(root/'last-screen.png').write_bytes(pixels)
        subprocess.run([str(adb),'shell','uiautomator','dump','/sdcard/bazzalt-window.xml'],capture_output=True,timeout=30)
        state=subprocess.run([str(adb),'shell','cat','/sdcard/bazzalt-window.xml'],capture_output=True,timeout=30).stdout
        (root/'window.txt').write_bytes(state)
        files=subprocess.run([str(adb),'shell','run-as',PACKAGE,'find','.','-type','f'],capture_output=True,timeout=30).stdout
        (root/'files.txt').write_bytes(files)
        logs=subprocess.run([str(adb),'logcat','-d'],capture_output=True,timeout=30).stdout
        (root/'logcat.txt').write_bytes(logs)
        subprocess.run([str(adb),'shell','am','force-stop',PACKAGE],capture_output=True,timeout=30)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--cooker',type=Path,required=True)
    arguments=parser.parse_args()
    Test(arguments.runtime.resolve(),arguments.cooker.resolve())
