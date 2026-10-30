"""Offline inventory verification for binaries the build host cannot execute."""
import hashlib
import json
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec,utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from .integrity import PlayerHash,SignatureInput
from .security import Derive
from .pak import SafeName

def Verify(folder,executable,master=None):
    folder=Path(folder);executable=Path(executable);data=executable.read_bytes()
    if len(data)<112:raise ValueError('Missing player seal')
    footer=data[-112:]
    if footer[:8]!=b'BZSIG003' or footer[72:80]!=b'BZKEY001':raise ValueError('Invalid player seal')
    boot_bytes=(folder/'game.bootstrap').read_bytes();payload=(folder/'game.integrity').read_bytes();signature=(folder/'game.signature').read_bytes()
    if len(boot_bytes)>65536 or len(payload)>64*1024**2 or len(signature)!=64:raise ValueError('Invalid inventory size')
    public=ec.EllipticCurvePublicNumbers(int.from_bytes(footer[8:40],'big'),int.from_bytes(footer[40:72],'big'),ec.SECP256R1()).public_key()
    public.verify(utils.encode_dss_signature(int.from_bytes(signature[:32],'big'),int.from_bytes(signature[32:],'big')),SignatureInput(boot_bytes,payload),ec.ECDSA(hashes.SHA256()))
    boot=json.loads(boot_bytes)
    if boot['format_version']!=3 or boot['player_hash']!=PlayerHash(executable,data=data[:-112]) or boot['seal_hash']!=hashlib.sha256(footer).hexdigest():raise ValueError('Player or seal hash mismatch')
    identity=bytes.fromhex(boot['build_id']);salt=bytes.fromhex(boot['salt']);root=None
    def decrypt(key,envelope,aad):return AESGCM(key).decrypt(envelope[:12],envelope[12:],aad)
    if boot['content_encrypted']:
        if master is None:raise ValueError('Cross-build verification requires the content master key')
        root=decrypt(Derive(master,salt,'wrapping'),bytes.fromhex(boot['wrapped_key']),identity)
    def manifest(stored,purpose):
        if boot['manifests_encrypted']:
            if not stored.startswith(b'BZENC003'):raise ValueError('Missing encrypted manifest')
            return json.loads(decrypt(Derive(root,salt,purpose),stored[8:],identity+purpose.encode('ascii')))
        return json.loads(stored)
    inventory=manifest(payload,'inventory');configuration=manifest((folder/'game.json').read_bytes(),'configuration')
    if inventory['format_version']!=3 or inventory['build_id']!=boot['build_id'] or configuration['format_version']!=3:raise ValueError('Mismatched game manifest')
    listed=set()
    for item in inventory['files']:
        name=item['path'];SafeName(name)
        if name in listed:raise ValueError('Duplicate signed file')
        path=folder.joinpath(*SafeName(name).parts)
        if path.is_symlink() or not path.resolve().is_relative_to(folder.resolve()) or path.stat().st_size!=item['size']:raise ValueError('Invalid signed file')
        with path.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
        if digest!=item['sha256']:raise ValueError('Signed file hash mismatch')
        listed.add(name)
    if 'game.json' not in listed:raise ValueError('Unsigned game configuration')
    allowed=listed|{executable.relative_to(folder).as_posix(),'game.bootstrap','game.integrity','game.signature','.bazzalt-game-build'}
    for path in folder.rglob('*'):
        if path.is_symlink() or path.is_file() and path.relative_to(folder).as_posix() not in allowed:raise ValueError('Unexpected or linked game output')
    return configuration
