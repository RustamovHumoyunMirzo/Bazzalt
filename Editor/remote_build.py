"""Verified SSH builds. Secrets stay in memory or the operating-system vault."""
import fnmatch
import stat
import hashlib
import json
import os
from pathlib import Path
import posixpath
import shlex
import tempfile
import threading
import time
import uuid
import zipfile
from bazzalt.settings import SettingsStore

DEFAULT=dict(id='',name='',host='',port=22,user='',authentication='key',private_key='',known_hosts='',worker_platform='linux',compiler='BazzaltCompiler',workspace='',runtime='',script_compiler='',sync_assets=True,clean=False,remove_intermediates=True,post_build='download',save_secret=False,exclusions='.env,*.pem,*.key,.ssh/**')

def Validate(profile):
    value=DEFAULT|profile
    for key,default in DEFAULT.items():
        if type(value[key]) is not type(default) or isinstance(value[key],str) and ('\0' in value[key] or len(value[key])>4096):raise ValueError('Invalid remote profile field: '+key)
    if value['authentication'] not in ('key','password') or value['worker_platform'] not in ('windows','linux','macos') or value['post_build'] not in ('download','leave_remote','open_remote'):raise ValueError('Invalid remote profile option')
    if not 1<=value['port']<=65535:raise ValueError('Invalid SSH port')
    return {key:value[key] for key in DEFAULT}

def Check(cancel):
    if cancel and cancel.is_set():raise ValueError('Remote build cancelled')

def Store():return SettingsStore('remote-builds',1,lambda:dict(schema_version=1,profiles=[]))

def Secret(profile,value=None):
    import keyring
    backend=keyring.get_keyring()
    allowed=('keyring.backends.Windows','keyring.backends.macOS','keyring.backends.SecretService','keyring.backends.kwallet','keyring.backends.libsecret')
    candidates=getattr(backend,'backends',None) or [backend]
    backend=next((item for item in candidates if type(item).__module__.startswith(allowed) and item.priority>0),None)
    if backend is None:raise ValueError('No secure operating-system credential store is available')
    account=profile['id']
    if not account:raise ValueError('Save the worker profile before storing credentials')
    if value is None:return backend.get_password('BAZZALT.RemoteBuild',account) or ''
    if value:backend.set_password('BAZZALT.RemoteBuild',account,value)
    else:
        try:backend.delete_password('BAZZALT.RemoteBuild',account)
        except keyring.errors.PasswordDeleteError:pass

def Command(profile,args):
    command=[profile['compiler'],*args]
    if profile['worker_platform']=='windows':
        import base64
        text="$ErrorActionPreference='Stop'; & "+ ' '.join("'"+str(v).replace("'","''")+"'" for v in command)+'; exit $LASTEXITCODE'
        return 'powershell -NoProfile -NonInteractive -EncodedCommand '+base64.b64encode(text.encode('utf-16le')).decode('ascii')
    return shlex.join(command)

def Connect(profile,secret=''):
    profile=Validate(profile)
    import paramiko
    if not profile['host'].strip() or not profile['user'].strip():raise ValueError('Host and username are required')
    if not 1<=profile['port']<=65535:raise ValueError('Invalid SSH port')
    client=paramiko.SSHClient();client.load_system_host_keys()
    if profile['known_hosts']:client.load_host_keys(profile['known_hosts'])
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    options=dict(hostname=profile['host'],port=profile['port'],username=profile['user'],timeout=15,banner_timeout=15,auth_timeout=15,allow_agent=profile['authentication']=='key',look_for_keys=profile['authentication']=='key')
    if profile['authentication']=='key':
        if profile['private_key']:options['key_filename']=profile['private_key']
        if secret:options['passphrase']=secret
    else:options.update(password=secret,allow_agent=False,look_for_keys=False)
    try:client.connect(**options);return client
    except Exception:client.close();raise

def Run(client,command,cancel=None,on_event=lambda event:None,timeout=7200):
    incoming,output,errors=client.exec_command(command,timeout=30);channel=output.channel;channel.settimeout(1);pending=b'';stderr=bytearray();result=None;started=time.monotonic();sent=False
    try:
        while True:
            if time.monotonic()-started>timeout:raise TimeoutError('Remote worker timed out')
            if cancel and cancel.is_set() and not sent:incoming.write('cancel\n');incoming.flush();sent=True
            while channel.recv_ready():
                pending+=channel.recv(65536)
                if len(pending)>1024*1024:raise ValueError('Remote response exceeds limits')
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1)
                    try:event=json.loads(line)
                    except (ValueError,UnicodeError):continue
                    if event.get('protocol')==1 and 'platform' in event:result=event
                    if event.get('stage')=='remote_complete':result=event
                    if event.get('stage')=='error':raise ValueError(event.get('message','Remote build failed'))
                    on_event(event)
            while channel.recv_stderr_ready():
                stderr.extend(channel.recv_stderr(8192));del stderr[:-16384]
            if channel.exit_status_ready() and not channel.recv_ready():break
            time.sleep(.03)
        if channel.recv_exit_status():raise ValueError(stderr.decode('utf-8','replace') or 'Remote worker command failed')
        if sent:raise ValueError('Remote build cancelled')
        if result is None:raise ValueError('Remote worker returned no valid protocol response')
        return result
    finally:channel.close()

def Test(profile,secret='',cancel=None,require_tools=True):
    client=Connect(profile,secret)
    try:
        Check(cancel);args=['--remote-info']+(['--runtime',profile['runtime']] if profile['runtime'] else [])+(['--script-compiler',profile['script_compiler']] if profile['script_compiler'] else []);info=Run(client,Command(profile,args),cancel,timeout=30)
        if require_tools and not info.get('cooker_ready',False):raise ValueError('Install the matching BazzaltCooker and runtime libraries beside the worker compiler')
        if require_tools and info.get('runtime_ready') is False:raise ValueError(info.get('runtime_error','Worker runtime SDK is invalid'))
        if info['platform']!=profile['worker_platform']:raise ValueError('The worker platform does not match its profile')
        return info
    finally:client.close()

def Package(project,archive,profile,cancel=None):
    import yaml
    project=Path(project).resolve();root=project.parent
    metadata=yaml.safe_load(project.read_text(encoding='utf-8-sig'));assets=metadata.get('AssetDirectory','Assets');asset_root=(root/assets).resolve()
    if not asset_root.is_relative_to(root) or asset_root==root:raise ValueError('Invalid project asset directory')
    excluded={'.git','__pycache__'};count=0;size=0
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=2) as target:
        for folder,dirs,names in os.walk(root,followlinks=False):
            dirs[:]=[d for d in dirs if d not in excluded and not (Path(folder)==root and d in ('.bazzalt','bin')) and not (Path(folder)/d).is_symlink()]
            for name in names:
                Check(cancel)
                source=Path(folder)/name
                if source.is_symlink():raise ValueError('Linked project files cannot be transferred')
                if not profile['sync_assets'] and source.resolve().is_relative_to(asset_root) and source.suffix.lower() not in ('.cpp','.h','.hpp','.lua','.meta','.bscene'):continue
                relative=source.relative_to(root).as_posix()
                if any(fnmatch.fnmatchcase(relative,pattern.strip()) or fnmatch.fnmatchcase(source.name,pattern.strip()) for pattern in profile.get('exclusions','').split(',') if pattern.strip()):continue
                size+=source.stat().st_size;count+=1
                if size>32*1024**3 or count>500000:raise ValueError('Remote project exceeds transfer limits')
                target.write(source,source.relative_to(root).as_posix())

def Mkdirs(sftp,path):
    if not path or path=='/':return
    try:
        value=sftp.lstat(path)
        if not stat.S_ISDIR(value.st_mode):raise ValueError('Remote workspace path is not a real directory')
        return
    except OSError as error:
        if getattr(error,'errno',2) not in (None,2):raise
    parent=posixpath.dirname(path)
    if parent==path:raise ValueError('Invalid remote workspace root')
    Mkdirs(sftp,parent);sftp.mkdir(path)

def Build(project,profile,secret='',*,cancel=None,on_event=lambda event:None):
    profile=Validate(profile);cancel=cancel or threading.Event();Check(cancel);client=Connect(profile,secret)
    try:
        from Compiler.settings import Load,Effective
        settings=Effective(Load(project))
        args=['--remote-info','--remote-architecture',settings['target']['architecture']]+(['--runtime',profile['runtime']] if profile['runtime'] else [])+(['--script-compiler',profile['script_compiler']] if profile['script_compiler'] else []);info=Run(client,Command(profile,args),cancel,timeout=40)
        if not info.get('cooker_ready',False):raise ValueError('Worker cooker is missing or does not match the compiler version')
        if info.get('runtime_ready') is False:raise ValueError(info.get('runtime_error','Worker runtime SDK is invalid'))
        if settings['security']['key_storage']=='dpapi_user' and profile['post_build']=='download':raise ValueError('Account-scoped DPAPI keys would bind the game to the worker account; select portable key delivery')
        if info['platform']!=settings['target']['platform']:raise ValueError('Select a worker matching the game target platform')
        if profile['clean'] and not profile['sync_assets']:raise ValueError('A clean build requires syncing all assets')
        sftp=client.open_sftp();job=None
        try:
            workspace=sftp.normalize(profile['workspace'] or '.')
            identity=hashlib.sha256(str(Path(project).resolve()).encode('utf-8')).hexdigest();job=posixpath.join(workspace,'.bazzalt-remote',uuid.uuid4().hex);Mkdirs(sftp,job)
            with tempfile.TemporaryDirectory(prefix='bazzalt-ssh-') as temporary:
                archive=Path(temporary)/'project.zip';on_event(dict(stage='sync'));Package(project,archive,profile,cancel)
                with archive.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
                Check(cancel);sftp.put(str(archive),job+'/project.zip',callback=lambda *_:Check(cancel));Check(cancel)
                request=dict(protocol=1,project_id=identity,project=Path(project).name,sha256=digest,runtime=profile['runtime'],compiler=profile['script_compiler'],clean=profile['clean'],sync_assets=profile['sync_assets'],remove_intermediates=profile['remove_intermediates'],open_output=profile['post_build']=='open_remote')
                with sftp.open(job+'/request.json','w') as file:file.write(json.dumps(request))
                native_request=job+'/request.json'
                if profile['worker_platform']=='windows' and native_request.startswith('/') and len(native_request)>3 and native_request[2]==':':native_request=native_request[1:]
                result=Run(client,Command(profile,['--remote-request',native_request]),cancel,on_event)
                if profile['post_build']=='download':
                    if type(result.get('size')) is not int or not 0<result['size']<=32*1024**3:raise ValueError('Invalid remote artifact size')
                    expected=posixpath.join(workspace,'.bazzalt-remote','projects',identity,'Project','bin')
                    remote_output=result['output'].replace('\\','/')
                    if profile['worker_platform']=='windows' and workspace.startswith('/') and len(remote_output)>1 and remote_output[1]==':':remote_output='/'+remote_output
                    normalized=sftp.normalize(remote_output)
                    expected=sftp.normalize(expected)
                    if posixpath.commonpath([expected,normalized])!=expected:raise ValueError('Remote artifact escaped the worker output folder')
                    folder=Path(project).parent/'bin/Remote';folder.mkdir(parents=True,exist_ok=True);name=Path(result['output'].replace('\\','/')).name
                    if not name.endswith('.zip') or '/' in name or name in ('.','..'):raise ValueError('Invalid remote output name')
                    incoming=folder/(uuid.uuid4().hex+'.part');Check(cancel);sftp.get(normalized,str(incoming),callback=lambda *_:Check(cancel))
                    try:
                        with incoming.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
                        if incoming.stat().st_size!=result['size'] or digest!=result['sha256']:raise ValueError('Remote artifact checksum mismatch')
                        destination=folder/(uuid.uuid4().hex[:8]+'-'+name);incoming.replace(destination);result['download']=str(destination)
                    finally:incoming.unlink(missing_ok=True)
                return result
        finally:
            if job:
                for name in ('project.zip','request.json'):
                    try:sftp.remove(job+'/'+name)
                    except OSError:pass
                try:sftp.rmdir(job)
                except OSError:pass
            sftp.close()
    finally:client.close()
