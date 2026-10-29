"""Native macOS binary inspection and application packaging; no engine builds."""
import hashlib
import plistlib
import struct
import subprocess
from pathlib import Path

CPU={0x01000007:'x64',0x0100000c:'arm64'}
def Architectures(path):
    with Path(path).open('rb') as f:
        header=f.read(8)
        if header[:4]==bytes.fromhex('cffaedfe'):
            cpu=struct.unpack_from('<I',header,4)[0]
            if cpu not in CPU:raise ValueError('Unsupported Mach-O CPU')
            return {CPU[cpu]}
        magic=header[:4];endian='>' if magic in (bytes.fromhex('cafebabe'),bytes.fromhex('cafebabf')) else '<'
        if magic not in tuple(bytes.fromhex(v) for v in ('cafebabe','cafebabf','bebafeca','bfbafeca')):raise ValueError('Not a supported Mach-O binary')
        count=struct.unpack_from(endian+'I',header,4)[0]
        if not 1<=count<=16:raise ValueError('Invalid universal binary')
        size=32 if magic in (bytes.fromhex('cafebabf'),bytes.fromhex('bfbafeca')) else 20
        result=set()
        for _ in range(count):
            entry=f.read(size)
            if len(entry)!=size:raise ValueError('Truncated universal binary')
            cpu=struct.unpack_from(endian+'I',entry)[0]
            if cpu not in CPU:raise ValueError('Unsupported universal CPU')
            result.add(CPU[cpu])
        return result

def PlayerHash(path):
    data=bytearray(Path(path).read_bytes())
    if len(data)<32 or data[:4]!=bytes.fromhex('cffaedfe'):raise ValueError('A native thin 64-bit Mach-O player is required')
    count,size=struct.unpack_from('<II',data,16);at=32;end=32+size;cut=None
    if end>len(data) or count>65536:raise ValueError('Invalid Mach-O load commands')
    for _ in range(count):
        if at+8>end:raise ValueError('Truncated Mach-O command')
        command,length=struct.unpack_from('<II',data,at)
        if length<8 or at+length>end:raise ValueError('Invalid Mach-O command size')
        if command==0x1d:
            if length!=16 or cut is not None:raise ValueError('Invalid code signature command')
            cut,signature_size=struct.unpack_from('<II',data,at+8)
            if cut<end or cut+signature_size>len(data):raise ValueError('Invalid code signature range')
            data[at+8:at+16]=bytes(8)
        if command==0x19 and length>=72 and data[at+8:at+24].rstrip(b'\0')==b'__LINKEDIT':
            data[at+32:at+40]=bytes(8);data[at+48:at+56]=bytes(8)
        at+=length
    if at!=end or cut is None:raise ValueError('Sign the macOS player before sealing')
    return hashlib.sha256(data[:cut]).hexdigest()

def Run(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,text=True,encoding='utf-8',errors='replace')
    if result.returncode:raise ValueError((result.stderr or result.stdout)[-12000:])

def CreateBundle(bundle,settings):
    profile=settings['platforms']['macos'];production=settings['production']
    contents=bundle/'Contents';root=contents/'MacOS';root.mkdir(parents=True)
    metadata=dict(CFBundlePackageType='APPL',CFBundleExecutable=production['name'],CFBundleIdentifier=profile['bundle_identifier'],CFBundleName=production['name'],CFBundleShortVersionString=production['version'],CFBundleVersion=production['version'],LSMinimumSystemVersion=profile['minimum_version'],NSHighResolutionCapable=True,NSPrincipalClass='NSApplication')
    (contents/'Info.plist').write_bytes(plistlib.dumps(metadata));return root

def Sign(path,profile):
    args=['/usr/bin/codesign','--force','--sign',profile['signing_identity']]
    if profile['signing_identity']!='-':args+=['--options','runtime','--timestamp']
    Run([*args,path])

def SignLibraries(root,profile):
    for path in sorted(Path(root).rglob('*')):
        if path.is_file() and path.suffix in ('.dylib','.so'):Sign(path,profile)

def Finalize(bundle,profile):
    Sign(bundle,profile)
    Run(['/usr/bin/codesign','--verify','--deep','--strict',bundle])
    if profile['notarization_profile']:
        archive=bundle.with_suffix('.notarize.zip');Archive(bundle,archive)
        try:
            Run(['xcrun','notarytool','submit',archive,'--keychain-profile',profile['notarization_profile'],'--wait'])
            Run(['xcrun','stapler','staple',bundle])
        finally:archive.unlink(missing_ok=True)

def Archive(bundle,destination):Run(['/usr/bin/ditto','-c','-k','--sequesterRsrc','--keepParent',bundle,destination])
