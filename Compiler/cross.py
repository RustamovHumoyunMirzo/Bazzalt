"""User-supplied cross toolchains and ABI-matched target SDKs; no engine builds."""
import contextvars
import json
import os
from pathlib import Path
import shutil
import subprocess
from bazzalt.platforms import HostPlatform,HostArchitecture

Active=contextvars.ContextVar('bazzalt_cross_toolchain',default=None)
METHODS={('linux','linux'):('zig',),('windows','windows'):('mingw','zig'),('windows','linux'):('wsl','zig'),('linux','windows'):('mingw','zig'),('macos','windows'):('mingw','zig'),('macos','linux'):('zig',)}

def Triple(platform,architecture,method):
    cpu={'x64':'x86_64','x86':'i686','arm64':'aarch64'}[architecture]
    if platform=='windows':return cpu+('-w64-mingw32' if method=='mingw' else '-windows-gnu')
    return cpu+'-linux-gnu'

def Detect(method,platform,architecture,override=''):
    if override:
        path=Path(override).expanduser()
        if not path.is_file():raise ValueError('The selected cross compiler does not exist')
        return path
    names=['zig'] if method=='zig' else [Triple(platform,architecture,'mingw')+'-g++'] if method=='mingw' else ['wsl.exe'] if method=='wsl' else ['clang++','clang++-18','clang++-17'] if method=='native' else []
    found=next((Path(value) for name in names if (value:=shutil.which(name))),None)
    if method=='wsl' and found:
        try:
            result=subprocess.run([str(found),'--list','--quiet'],capture_output=True,timeout=5,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            # Windows WSL emits UTF-16; only presence matters, not distro names.
            if result.returncode or not result.stdout.replace(b'\0',b'').strip():return None
        except (OSError,subprocess.TimeoutExpired):return None
    return found

def Resolve(settings,runtime):
    host=HostPlatform();target=settings['target'];platform=target['platform'];profile=settings['platforms'][platform];method=profile.get('build_method','automatic')
    if settings['security']['key_storage']=='dpapi_user' and not CanExecute(platform,target['architecture']):raise ValueError('Account-scoped DPAPI protection requires this builder account and machine; use portable key delivery for cross builds')
    if method=='content_only':
        if platform=='macos' and host!='macos':raise ValueError('macOS application signing requires a Mac worker')
        if settings['security']['key_storage']=='dpapi_user' and host!='windows':raise ValueError('DPAPI protection requires a Windows worker')
        return dict(method='content_only',compiler='',platform=platform)
    if method=='wsl' and host=='linux':method='native'
    if host==platform and method in ('automatic','native') and not (host=='linux' and target['architecture'] not in (HostArchitecture(),'x86')):return None
    choices=METHODS.get((host,platform),())
    if method=='automatic':method=next((name for name in choices if Detect(name,platform,target['architecture'])), '')
    if method not in choices:raise ValueError('No supported cross-build method for this host and target')
    compiler=Detect(method,platform,target['architecture'],profile.get('toolchain',''))
    if not compiler:raise ValueError('Install the selected cross toolchain or select its executable in Build Settings')
    if method=='wsl':return dict(method=method,compiler=str(compiler))
    manifest=json.loads((Path(runtime)/'runtime.json').read_text(encoding='utf-8-sig'))
    tool=manifest.get('cross_toolchain',{})
    version=subprocess.run([str(compiler),'version' if method=='zig' else '-dumpfullversion'],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=15,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    actual=version.stdout.strip()
    if version.returncode or not actual:raise ValueError('Could not inspect the cross compiler version')
    # C++ values cross the public SDK boundary. Merely accepting COFF import
    # libraries does not make a GNU/Clang C++ ABI match an MSVC-built core.
    if tool.get('method')!=method or tool.get('version')!=actual or tool.get('triple')!=Triple(platform,target['architecture'],method):raise ValueError('The target runtime must declare an exact matching cross_toolchain method, version and triple. Native MSVC/libc++ SDKs are not interchangeable with GNU/Zig SDKs.')
    if settings['security']['key_storage']=='dpapi_user':raise ValueError('Builder-account DPAPI protection requires a native Windows build')
    return dict(method=method,compiler=str(compiler),triple=tool['triple'],platform=platform,sysroot=profile.get('sysroot',''))

def LinkLibrary(sdk,options):
    names=('libBazzalt.dll.a','Bazzalt.lib') if options['platform']=='windows' else ('libBazzalt.so',)
    return next((Path(sdk)/'lib'/name for name in names if (Path(sdk)/'lib'/name).is_file()),None)

def Configure(command,options):
    result=[options['compiler']]
    if options['method']=='zig':result+=['c++','-target',options['triple']]
    result+=command[1:]
    if options.get('sysroot'):result+=['--sysroot='+options['sysroot']]
    if options['platform']=='windows':result+=['-Wl,--no-undefined']
    else:result+=['-fPIC','-Wl,--no-undefined','-Wl,-rpath,$ORIGIN']
    return result

def WslCommand(project,profile,cancel=None):
    tool=Detect('wsl','linux',profile['architecture'],profile.get('toolchain',''))
    if not tool:raise ValueError('Install WSL and a Linux distribution first')
    prefix=[str(tool)]
    if profile.get('wsl_distribution'):prefix+=['--distribution',profile['wsl_distribution']]
    mapped=subprocess.run([*prefix,'--','wslpath','-a','-u',str(Path(project).resolve())],capture_output=True,encoding='utf-8',timeout=30,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    if mapped.returncode:raise ValueError(mapped.stderr or 'WSL could not map this project path')
    executable=profile.get('wsl_compiler','')
    if not executable:
        found=subprocess.run([*prefix,'--','sh','-lc','command -v BazzaltCompiler'],capture_output=True,encoding='utf-8',timeout=30,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        executable=found.stdout.strip()
    if not executable:raise ValueError('Install the Linux BazzaltCompiler in WSL, or select its WSL path')
    runtime=profile.get('wsl_runtime','') or str(Path(executable).parent/'RuntimeTemplates'/('linux-'+profile['architecture'])).replace('\\','/')
    return [*prefix,'--',executable,'--project',mapped.stdout.strip(),'--runtime',runtime,'--cooker',str(Path(executable).parent/'BazzaltCooker').replace('\\','/')]


def CanExecute(platform,architecture):
    if platform!=HostPlatform():return False
    if architecture==HostArchitecture():return True
    if platform=='windows' and HostArchitecture()=='arm64':return architecture in ('x64','x86')
    return platform in ('windows','linux') and HostArchitecture()=='x64' and architecture=='x86'
