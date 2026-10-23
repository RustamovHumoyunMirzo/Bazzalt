"""Per-build signed inventory, pinned in the native player's executable.

This protects an unchanged player from content modification, not a patched
executable. Publisher trust still requires external executable code signing.
"""
import hashlib
import json
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from .pak import SafeName


def Seal(folder, executable, key=None):
    folder=Path(folder);executable=Path(executable)
    signing=ec.generate_private_key(ec.SECP256R1())
    records=[]
    for path in sorted(folder.rglob('*')):
        if path.is_symlink():raise ValueError('Linked build output is forbidden')
        if not path.is_file() or path==executable or path.relative_to(folder).as_posix() in {'game.integrity','game.signature','.bazzalt-game-build'}:continue
        name=path.relative_to(folder).as_posix();SafeName(name)
        with path.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
        records.append(dict(path=name,size=path.stat().st_size,sha256=digest))
    payload=json.dumps(dict(format_version=1,files=records),ensure_ascii=True,separators=(',',':')).encode('ascii')
    signature=signing.sign(payload,ec.ECDSA(hashes.SHA256()));r,s=utils.decode_dss_signature(signature)
    (folder/'game.integrity').write_bytes(payload)
    (folder/'game.signature').write_bytes(r.to_bytes(32,'big')+s.to_bytes(32,'big'))
    public=signing.public_key().public_numbers()
    # Keep the existing key trailer so key delivery has a single parser.
    with executable.open('ab') as file:
        file.write(b'BZSIG002'+public.x.to_bytes(32,'big')+public.y.to_bytes(32,'big'))
        file.write(b'BZKEY001'+(key if key is not None else bytes(32)))
