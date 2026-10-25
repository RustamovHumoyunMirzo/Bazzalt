"""Shared native host/target contract. Cross-compilation is never implicit."""
import platform
import sys

def HostPlatform():
    return 'windows' if sys.platform=='win32' else 'linux' if sys.platform.startswith('linux') else 'unsupported'

def TargetError(target):
    selected=target.get('platform','windows');architecture=target.get('architecture','x64');host=HostPlatform()
    if selected not in ('windows','linux'):return f'Unsupported target platform: {selected}'
    if host!=selected:return f'{selected.title()} builds require a {selected.title()} editor/compiler and matching runtime SDK. Current host: {host}.'
    if selected=='linux' and architecture!='x64':return 'Linux game builds currently require the x64 architecture.'
    if platform.machine().lower() not in ('amd64','x86_64'):return 'The editor and game compiler require an x64 machine.'
    if selected=='linux' and target.get('key_storage')=='dpapi_user':return 'Windows account key protection is unavailable on Linux.'
    return ''
