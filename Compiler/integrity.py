"""Per-build signed inventory, pinned in the native player's executable.

This protects an unchanged player from content modification, not a patched
executable. Publisher trust still requires external executable code signing.
"""
import hashlib
import json
import struct
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from .pak import SafeName


def PlayerHash(executable,*,data=None):
    # Match the native reader: PE checksum and certificate-directory entries
    # change during Authenticode signing; executable code/resources do not.
    data=bytearray(Path(executable).read_bytes() if data is None else data)
    if data[:4]==bytes.fromhex('cffaedfe'):
        from .macos import PlayerHash as MacHash
        return MacHash(executable)
    if data[:4]==b'\x7fELF':return hashlib.sha256(data).hexdigest()
    if len(data)<64 or data[:2]!=b'MZ':raise ValueError('Invalid player executable')
    pe=struct.unpack_from('<I',data,60)[0];optional=pe+24
    if optional+152>len(data) or data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid player PE header')
    magic=struct.unpack_from('<H',data,optional)[0]
    if magic not in (0x10b,0x20b):raise ValueError('Unsupported player PE format')
    directory=optional+(96 if magic==0x10b else 112)+32
    data[optional+64:optional+68]=bytes(4);data[directory:directory+8]=bytes(8)
    return hashlib.sha256(data).hexdigest()


def SignatureInput(bootstrap,payload):
    return b'BZSIG003'+struct.pack('<I',len(bootstrap))+bootstrap+payload


def Seal(folder, executable, *, security=None, content=None):
    folder=Path(folder);executable=Path(executable)
    if security is None:
        from .security import SecurityContext
        from .settings import DEFAULTS
        security=SecurityContext(DEFAULTS)
    signing=ec.generate_private_key(ec.SECP256R1());public=signing.public_key().public_numbers()
    seal=b'BZSIG003'+public.x.to_bytes(32,'big')+public.y.to_bytes(32,'big')+b'BZKEY001'+security.FooterKey
    bootstrap=security.Bootstrap(folder,PlayerHash(executable),hashlib.sha256(seal).hexdigest())
    configuration=folder/'game.json'
    configuration.write_bytes(security.ProtectManifest(configuration.read_bytes(),'configuration'))
    records=[]
    for path in sorted(folder.rglob('*')):
        if path.is_symlink() or path.is_junction():raise ValueError('Linked build output is forbidden')
        if not path.is_file() or path==executable or path.relative_to(folder).as_posix() in {'game.bootstrap','game.integrity','game.signature','.bazzalt-game-build','game.seal'}:continue
        name=path.relative_to(folder).as_posix();SafeName(name)
        with path.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
        records.append(dict(path=name,size=path.stat().st_size,sha256=digest))
    payload=json.dumps(dict(format_version=3,build_id=security.BuildId.hex(),files=records,content=content or {}),ensure_ascii=True,separators=(',',':')).encode('ascii')
    payload=security.ProtectManifest(payload,'inventory')
    if len(payload)>64*1024*1024:raise ValueError('Signed inventory exceeds the native player limit')
    signature=signing.sign(SignatureInput(bootstrap,payload),ec.ECDSA(hashes.SHA256()));r,s=utils.decode_dss_signature(signature)
    (folder/'game.bootstrap').write_bytes(bootstrap)
    (folder/'game.integrity').write_bytes(payload)
    (folder/'game.signature').write_bytes(r.to_bytes(32,'big')+s.to_bytes(32,'big'))
    # Keep the existing key trailer so key delivery has a single parser.
    if executable.read_bytes()[:4]==bytes.fromhex('cffaedfe'):(folder/'game.seal').write_bytes(seal)
    else:
        with executable.open('ab') as file:file.write(seal)
