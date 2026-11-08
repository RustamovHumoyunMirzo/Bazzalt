"""Build Android packages from a prebuilt runtime SDK and host-side content tools."""
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import uuid
import zipfile
from xml.sax.saxutils import escape

from .resources import BrandingPNG
from .android_tools import Validate
from .build import (BuildLock, BuildWorkspace, CheckCancelled, CompileScripts, CookRuntimeAssets,
                    Emit, KeyFor, Pack, Publish, Run, ValidateCooker, ValidateRuntime)


def Digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def StageModules(cooked,libraries,architecture):
    from .build import Architecture
    dependencies=set()
    index=cooked/'ext/runtime.json'
    if index.is_file():
        dependencies={(Path('ext')/name).as_posix() for name in json.loads(index.read_text(encoding='utf-8')).get('NativeLibraries',[])}
    for relative in dependencies:
        if not relative.endswith('.so'):raise ValueError('Android native dependencies must use unversioned libNAME.so libraries')
    mapping={}
    for file in sorted(cooked.rglob('*.so')):
        relative=file.relative_to(cooked).as_posix()
        with file.open('rb') as binary:magic=binary.read(4)
        if magic!=b'\x7fELF' or Architecture(file)!=architecture:raise ValueError('Android native module architecture mismatch: '+relative)
        if relative in dependencies:
            name=file.name
            if not re.fullmatch(r'lib[A-Za-z0-9_.-]+\.so',name):raise ValueError('Android native dependencies require libNAME.so filenames')
        else:name='libasset_'+hashlib.sha256(relative.encode('utf-8')).hexdigest()+'.so'
        target=libraries/name
        if target.exists() and Digest(target)!=Digest(file):raise ValueError('Conflicting Android native library: '+name)
        if not target.exists():shutil.copy2(file,target)
        mapping[relative]=name
    return mapping


def ValidateLibraries(libraries,tools,minimum_api):
    """Reject missing/too-new dependencies and incompatible 16 KiB load segments."""
    system=json.loads((tools.Ndk/'meta/system_libs.json').read_text(encoding='utf-8'))
    available={file.name for file in libraries.glob('*.so')}
    available.update(name for name,api in system.items() if int(api)<=minimum_api)
    readelf=tools.Compiler.parent/('llvm-readelf.exe' if os.name=='nt' else 'llvm-readelf')
    for library in libraries.glob('*.so'):
        with library.open('rb') as file:header=file.read(20)
        if len(header)<20 or header[:4]!=b'\x7fELF' or header[5]!=1 or int.from_bytes(header[16:18],'little')!=3:
            raise ValueError('Invalid Android shared library: '+library.name)
        output=Run([readelf,'-lW','-d',library]).stdout
        missing=set(re.findall(r'\(NEEDED\).*Shared library: \[(.*?)\]',output))-available
        if missing:raise ValueError(library.name+' requires unavailable libraries at the minimum SDK: '+', '.join(sorted(missing)))
        loads=[line.split() for line in output.splitlines() if line.strip().startswith('LOAD ')]
        if not loads or any(int(fields[-1],16)<16384 for fields in loads):
            raise ValueError(library.name+' must be rebuilt with 16 KiB ELF page alignment')


def Manifest(settings, configuration, split=False):
    profile = settings['platforms']['android']
    package = profile['application_id']
    common = (f'package="{package}" android:versionCode="{profile["version_code"]}" '
              f'android:versionName="{escape(settings["production"]["version"], {chr(34): "&quot;"})}"')
    if split:
        return (f'<manifest xmlns:android="http://schemas.android.com/apk/res/android" {common} '
                'split="config.bazzalt_content"><application android:hasCode="false"/></manifest>')
    label = escape(settings['production']['name'], {'"': '&quot;'})
    return f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android" {common}>
    <uses-sdk android:minSdkVersion="{profile['min_sdk']}" android:targetSdkVersion="{profile['target_sdk']}"/>
    <uses-permission android:name="android.permission.INTERNET"/>
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
    <uses-feature android:glEsVersion="0x00030000" android:required="true"/>
    <application android:label="{label}" android:icon="@mipmap/ic_launcher" android:theme="@android:style/Theme.Material.NoActionBar"
        android:allowBackup="false" android:usesCleartextTraffic="false" android:extractNativeLibs="true"
        android:debuggable="{'true' if configuration == 'debug' else 'false'}">
        <activity android:name="com.bazzalt.runtime.BootstrapActivity" android:exported="true" android:screenOrientation="{profile['orientation']}">
            <intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
        </activity>
        <activity android:name="com.bazzalt.runtime.GameActivity" android:exported="false" android:screenOrientation="{profile['orientation']}"
            android:configChanges="keyboard|keyboardHidden|navigation|orientation|screenLayout|screenSize|smallestScreenSize|uiMode|density"
            android:launchMode="singleTask" android:hardwareAccelerated="true"/>
    </application></manifest>'''


def Resources(directory, icon, project):
    from PIL import Image
    from .icons import Validate as ValidateIcon
    if icon:
        path=Path(icon)
        if not path.is_absolute():path=project.parent/path
        ValidateIcon(path,'android')
        source=path
    else:source=io.BytesIO(BrandingPNG())
    image=Image.open(source).convert('RGBA')
    for density, size in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        folder = directory / ('mipmap-' + density)
        folder.mkdir(parents=True)
        image.resize((size,size),Image.Resampling.LANCZOS).save(folder/'ic_launcher.png')
        image.resize((round(size*2.25),round(size*2.25)),Image.Resampling.LANCZOS).save(folder/'ic_launcher_foreground.png')
    adaptive=directory/'mipmap-anydpi-v26'
    adaptive.mkdir()
    (adaptive/'ic_launcher.xml').write_text('<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android"><background android:drawable="@android:color/black"/><foreground android:drawable="@mipmap/ic_launcher_foreground"/></adaptive-icon>',encoding='utf-8')



def Signing(tools, profile, configuration, cache):
    if configuration == 'debug':
        keystore = cache / 'debug.keystore'
        keytool = tools.Java.parent / ('keytool.exe' if os.name == 'nt' else 'keytool')
        if not keystore.exists():
            Run([keytool, '-genkeypair', '-keystore', keystore, '-storepass', 'android', '-keypass', 'android',
                 '-alias', 'androiddebugkey', '-dname', 'CN=Android Debug,O=Android,C=US', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000'])
        return ['--ks', keystore, '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android']
    keystore = Path(profile['signing_keystore']).expanduser()
    if not profile['signing_keystore'] or not keystore.is_file() or not profile['signing_alias']:
        raise ValueError('Release APKs require a signing keystore and alias in Android Build Settings')
    variables = (profile['store_password_environment'], profile['key_password_environment'])
    if any(not os.environ.get(name) for name in variables):
        raise ValueError('Set the configured Android signing password environment variables')
    return ['--ks', keystore.resolve(), '--ks-key-alias', profile['signing_alias'], '--ks-pass', 'env:' + variables[0], '--key-pass', 'env:' + variables[1]]


def Package(tools, settings, configuration, work, output, payload, libraries, runtime, signing, check):
    profile = settings['platforms']['android']
    suffix = '.exe' if os.name == 'nt' else ''
    aapt = tools.BuildTools / ('aapt2' + suffix)
    resources = work / 'res'
    Resources(resources, profile['icon'], Path(settings['_project']))
    compiled = work / 'resources.zip'
    Run([aapt, 'compile', '--dir', resources, '-o', compiled])
    source_root = Path(__file__).parent / 'android/java'
    sources = [*source_root.rglob('*.java'), *(runtime / 'android/java').rglob('*.java')]
    if not any(p.name == 'SDLActivity.java' for p in sources): raise ValueError('Android runtime SDK is missing SDL Java sources')
    classes = work / 'classes'
    classes.mkdir()
    # javac argument files avoid Windows command-line limits and preserve Unicode paths.
    arguments = work / 'java.args'
    arguments.write_text('\n'.join('"' + str(p.resolve()).replace('\\', '/').replace('"', '\\"') + '"' for p in sources), encoding='utf-8')
    Run([tools.Javac, '-J-Dfile.encoding=UTF-8', '-encoding', 'UTF-8', '--release', '8', '-classpath', tools.Jar, '-d', classes, '@' + str(arguments)])
    classes_jar = work / 'classes.jar'
    with zipfile.ZipFile(classes_jar, 'w', zipfile.ZIP_DEFLATED) as archive:
        for file in classes.rglob('*.class'): archive.write(file, file.relative_to(classes).as_posix())
    dex = work / 'dex'
    dex.mkdir()
    Run([tools.Java, '-Dfile.encoding=UTF-8', '-cp', tools.BuildTools / 'lib/d8.jar', 'com.android.tools.r8.D8', '--lib', tools.Jar,
         '--min-api', str(profile['min_sdk']), '--output', dex, classes_jar])
    mode = profile['delivery']
    delivery = dict(id=uuid.uuid4().hex, mode=mode, files=[], native_modules=settings.get('_native_modules',{}))
    for file in sorted(payload.rglob('*')):
        check()
        if file.is_file():
            delivery['files'].append(dict(path=file.relative_to(payload).as_posix(), size=file.stat().st_size,
                                          sha256=Digest(file)))
    base_assets = work / 'assets'
    base_assets.mkdir()
    (base_assets/'branding.png').write_bytes(BrandingPNG())
    if mode == 'apk': shutil.copytree(payload, base_assets / 'bazzalt')
    elif mode in ('obb', 'external_data'):
        delivery['archive'] = f'main.{profile["version_code"]}.{profile["application_id"]}.obb' if mode == 'obb' else 'game-data.zip'
        with zipfile.ZipFile(output / delivery['archive'], 'w', zipfile.ZIP_STORED, allowZip64=True) as archive:
            for file in payload.rglob('*'):
                if file.is_file(): archive.write(file, file.relative_to(payload).as_posix())
    inventory=json.dumps(delivery).encode('utf-8')
    if len(delivery['files'])>100000 or len(inventory)>8388608:raise ValueError('Android content inventory exceeds launcher limits')
    (base_assets/'delivery.json').write_bytes(inventory)
    apks = []
    for split in ([False, True] if mode == 'split_apk' else [False]):
        check()
        manifest = work / ('split.xml' if split else 'AndroidManifest.xml')
        manifest.write_text(Manifest(settings, configuration, split), encoding='utf-8')
        unsigned = work / ('split-unsigned.apk' if split else 'base-unsigned.apk')
        assets = base_assets
        if split:
            assets = work / 'split-assets'
            shutil.copytree(payload, assets / 'bazzalt')
        command = [aapt, 'link', '-I', tools.Jar, '--manifest', manifest, '-A', assets, '-o', unsigned]
        if not split: command.append(compiled)
        Run(command)
        if not split:
            with zipfile.ZipFile(unsigned, 'a', zipfile.ZIP_STORED, allowZip64=True) as archive:
                for file in dex.glob('*.dex'): archive.write(file, file.name)
                for file in sorted(libraries.glob('*.so')): archive.write(file, f'lib/{tools.Abi}/{file.name}')
        aligned = work / ('split-aligned.apk' if split else 'base-aligned.apk')
        Run([tools.BuildTools / ('zipalign' + suffix), '-P', '16', '-f', '4', unsigned, aligned])
        target = output / ('content.apk' if split else 'base.apk')
        Run([tools.Java, '-Dfile.encoding=UTF-8', '-jar', tools.BuildTools / 'lib/apksigner.jar', 'sign', *signing, '--out', target, aligned])
        Run([tools.Java, '-Dfile.encoding=UTF-8', '-jar', tools.BuildTools / 'lib/apksigner.jar', 'verify', '--verbose', target])
        apks.append(target)
    return apks, delivery


def Launch(tools, profile, output, delivery):
    adb = tools.Sdk / 'platform-tools' / ('adb.exe' if os.name == 'nt' else 'adb')
    devices = Run([adb, 'devices']).stdout.splitlines()[1:]
    devices = [line.split()[0] for line in devices if line.endswith('\tdevice')]
    if len(devices) != 1: raise ValueError('Connect exactly one authorized Android device or emulator for Build and Launch')
    command = [adb, '-s', devices[0]]
    apks = [output / 'base.apk']
    if profile['delivery'] == 'split_apk': apks.append(output / 'content.apk')
    Run([*command, 'install-multiple' if len(apks) > 1 else 'install', '-r', *apks])
    package = profile['application_id']
    activity = package + '/com.bazzalt.runtime.BootstrapActivity'
    if 'archive' in delivery:
        Run([*command, 'shell', 'am', 'start', '-W', '-n', activity])
        root = '/sdcard/Android/' + ('obb' if profile['delivery'] == 'obb' else 'data') + '/' + package
        if profile['delivery'] == 'external_data': root += '/files'
        Run([*command, 'shell', 'mkdir', '-p', root])
        Run([*command, 'push', output / delivery['archive'], root + '/' + delivery['archive']])
        Run([*command, 'shell', 'am', 'force-stop', package])
    Run([*command, 'shell', 'am', 'start', '-W', '-n', activity])


def BuildAndroid(project, runtime, cooker, settings, configuration, launch, cancel):
    from bazzalt.platforms import HostPlatform
    from .cross import Active, Resolve
    from .content import FilterCooked
    from .extensions import StageGame
    from .integrity import Seal
    from .security import SecurityContext
    from .verify import Verify
    project = Path(project).resolve()
    profile = settings['platforms']['android']
    arch = profile['architecture']
    runtime, files = ValidateRuntime(runtime, arch, 'android')
    if profile['signing_keystore'] and not Path(profile['signing_keystore']).is_absolute():
        profile['signing_keystore']=str(project.parent/profile['signing_keystore'])
    tools = Validate(profile, arch, runtime)
    cooker = ValidateCooker(cooker, HostPlatform())
    settings['_project'] = str(project)
    token = Active.set(Resolve(settings, runtime))
    try:
        with BuildLock(project.parent) as cache, BuildWorkspace(cache / configuration.title() / ('android-' + arch)) as work:
            check = lambda: CheckCancelled(cancel)
            signing = Signing(tools, profile, configuration, cache)
            cooked = work / 'Cooked'
            cooked.mkdir()
            Emit('scripts', 'Compiling Android game scripts', architecture=arch)
            from .extensions import LockedItems,MaterializeAssets
            extension_items=LockedItems(project,'android',arch)
            if extension_items:MaterializeAssets(project,extension_items)
            bindings = CompileScripts(project, runtime, arch, cache / configuration.title() / ('android-' + arch), configuration=configuration)
            for binding in bindings:
                binding['properties'] = {key: ('true' if value is True else 'false' if value is False else ','.join(map(str, value)) if isinstance(value, list) else str(value)) for key, value in binding['properties'].items()}
            manifest = work / 'bindings.json'
            manifest.write_text(json.dumps(dict(startup=settings['target']['startup_scene'], bindings=bindings), ensure_ascii=False), encoding='utf-8')
            Emit('cook', 'Cooking Android game content')
            environment = dict(os.environ, BAZZALT_FILAMENT_TOOLS=str(cooker.parent / 'filament'))
            Run([cooker, project, cooked, manifest], cwd=cooker.parent, env=environment)
            check()
            CookRuntimeAssets(cooked, cooker, check, platform='mobile')
            FilterCooked(cooked, settings['packaging'])
            StageGame(project, cooked, 'android', arch, runtime)
            forbidden={'.py','.pyc','.pyd','.cpp','.hpp','.h','.cc','.cxx','.lua'}
            for file in cooked.rglob('*'):
                if file.is_file() and file.suffix.lower() in forbidden:raise ValueError('Source file leaked into Android content: '+str(file))
            libraries = work / 'libraries' 
            libraries.mkdir()
            for name in files: shutil.copy2(runtime / name, libraries / name)
            settings['_native_modules']=StageModules(cooked,libraries,arch)
            strip=tools.Compiler.parent/('llvm-strip.exe' if os.name=='nt' else 'llvm-strip')
            for library in libraries.glob('*.so'):
                if configuration=='debug' and library.name not in files:continue
                check();Run([strip,'--strip-unneeded',library])
            ValidateLibraries(libraries,tools,profile['min_sdk'])
            payload = work / 'payload'
            payload.mkdir()
            security = SecurityContext(settings, KeyFor(settings))
            hashes = {}
            packages = []
            if settings['packaging']['format'] == 'pak':
                packages = [p.name for p in Pack(cooked, payload, settings['packaging']['chunk_mb'], security.PackageKey, hashes=hashes, check=check, portable=True)]
            else: shutil.copytree(cooked, payload / 'Data')
            if settings['packaging']['decoy_kb']:(payload/'padding.bin').write_bytes(os.urandom(settings['packaging']['decoy_kb']*1024))
            game = dict(format_version=3, configuration=configuration, format=settings['packaging']['format'],
                        production=settings['production'], window=settings['window'], intro=settings['intro'], packages=packages,
                        **{k: settings['security'][k] for k in ('encryption', 'key_storage', 'key_environment')})
            (payload / 'game.json').write_text(json.dumps(game), encoding='utf-8')
            (payload / 'LICENSE.txt').write_text(settings['production']['license'], encoding='utf-8')
            if (runtime / 'licenses').is_dir(): shutil.copytree(runtime / 'licenses', payload / 'licenses')
            Seal(payload, libraries / 'libmain.so', security=security, content=hashes)
            Verify(payload, libraries / 'libmain.so', security.Master)
            publication = work / 'Game'
            publication.mkdir()
            Emit('package', 'Building and signing Android packages')
            _, delivery = Package(tools, settings, configuration, work, publication, payload, libraries, runtime, signing, check)
            destination = project.parent / 'bin' / configuration.title()
            destination.mkdir(parents=True, exist_ok=True)
            output = destination / (settings['production']['name'] + '-android-' + arch)
            check()
            owner = hashlib.sha256(str(project).encode('utf-8')).hexdigest()
            result=output
            launch_folder=publication
            if settings['packaging']['archive']:
                archive=destination/(output.name+'.zip')
                marker=archive.with_suffix('.zip.owner')
                if archive.exists() and (not marker.is_file() or marker.read_text()!=owner):raise ValueError('Archive is not owned by this project')
                temporary=work/'Game.zip'
                with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as zipped:
                    for file in publication.rglob('*'):
                        check()
                        if file.is_file():zipped.write(file,file.relative_to(publication).as_posix())
                check();os.replace(temporary,archive);marker.write_text(owner,encoding='utf-8');result=archive
            else:
                Publish(publication,output,owner)
                launch_folder=output
            if launch:
                try:Launch(tools,profile,launch_folder,delivery)
                except (OSError,ValueError) as error:Emit('launch_error',str(error))
            Emit('complete', 'Android build finished', output=str(result), configuration=configuration)
            return result
    finally: Active.reset(token)
