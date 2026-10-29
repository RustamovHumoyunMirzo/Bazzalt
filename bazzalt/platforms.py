"""Shared native host/target contract. Builds use a matching native SDK."""
import platform
import sys

def HostPlatform():
    return 'windows' if sys.platform=='win32' else 'macos' if sys.platform=='darwin' else 'linux' if sys.platform.startswith('linux') else 'unsupported'

def HostArchitecture():
    return 'arm64' if platform.machine().lower() in ('arm64','aarch64') else 'x64' if platform.machine().lower() in ('amd64','x86_64') else 'unsupported'

def TargetError(target):
    selected=target.get('platform','windows');architecture=target.get('architecture','x64');host=HostPlatform()
    if selected not in ('windows','linux','macos'):return f'Unsupported target platform: {selected}'
    if host!=selected:return f'{selected.title()} builds require a {selected.title()} editor/compiler and matching runtime SDK. Current host: {host}.'
    if architecture not in (('x64','arm64') if selected=='macos' else ('x64','x86')):return 'Unsupported game architecture: '+str(architecture)
    if HostArchitecture()=='unsupported' or host!='macos' and HostArchitecture()!='x64':return 'The editor and game compiler require a supported 64-bit machine.'
    if selected=='macos' and architecture!=HostArchitecture():return 'Select a matching native macOS SDK/editor architecture; cross-architecture builds are not implicit.'
    if selected!='windows' and target.get('key_storage')=='dpapi_user':return 'Windows account key protection is unavailable on this platform.'
    if selected!='macos' and target.get('key_storage')=='keychain':return 'Game Keychain storage requires macOS.'
    return ''
