"""BPAK v1: bounded independently authenticated file segments. No custom crypto."""
import hashlib
import os
from pathlib import Path,PurePosixPath
import struct

MAGIC=b'BZPAK001'
HEADER=struct.Struct('<IIQQB')
MAX_SEGMENT=4*1024*1024
def SafeName(name):
    path=PurePosixPath(name)
    if not name or len(name.encode('utf-8'))>4096 or any(ord(c)<32 for c in name) or path.is_absolute() or '\\' in name or ':' in name or any(p in ('','.','..') or p.endswith((' ','.')) for p in name.split('/')):raise ValueError('Unsafe package path')
    reserved={'CON','PRN','AUX','NUL',*[f'COM{i}' for i in range(1,10)],*[f'LPT{i}' for i in range(1,10)]}
    if any(any(c in part for c in '<>"|?*') or part.split('.')[0].upper() in reserved for part in path.parts):raise ValueError('Reserved package path')
    return path
def Pack(source,destination,chunk_mb=64,key=None):
    aes=None
    if key is not None:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        if len(key)!=32:raise ValueError('AES-256 needs a 32-byte key')
        aes=AESGCM(key)
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True);outputs=[];stream=None;size=0
    try:
        for path in sorted(Path(source).rglob('*')):
            if path.is_symlink():raise ValueError('Symlinks are not allowed in game packages')
            if not path.is_file():continue
            name=path.relative_to(source).as_posix();SafeName(name);encoded=name.encode('utf-8');total=path.stat().st_size;offset=0
            with path.open('rb') as file:
                while offset<total or offset==0:
                    data=file.read(MAX_SEGMENT);header=HEADER.pack(len(encoded),len(data),offset,total,bool(aes));digest=hashlib.sha256(data).digest();nonce=os.urandom(12);tag=bytes(16)
                    if aes:data=aes.encrypt(nonce,data,header+encoded);data,tag=data[:-16],data[-16:]
                    record=header+encoded+nonce+tag+digest+data
                    if stream is None or size+len(record)>chunk_mb*1024*1024:
                        if stream:stream.close()
                        output=destination/f'content-{len(outputs):05d}.bpak';outputs.append(output);stream=output.open('xb');stream.write(MAGIC);size=len(MAGIC)
                    stream.write(record);size+=len(record);offset+=len(data)
                    if offset>=total:break
        return outputs
    finally:
        if stream:stream.close()
