"""Version 3 envelope encryption. Only standard AES-GCM / HKDF primitives."""
import ctypes
import json
import os
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF


def Derive(key, salt, purpose):
    return HKDF(algorithm=hashes.SHA256(), length=32, salt=salt,
                info=('Bazzalt/v3/'+purpose).encode('ascii')).derive(key)


def Encrypt(key, data, aad):
    nonce=os.urandom(12)
    return nonce+AESGCM(key).encrypt(nonce,data,aad)


def ProtectForCurrentUser(data, entropy):
    """DPAPI user scope is intentionally not portable to other players."""
    if os.name!='nt':raise ValueError('Current-user key protection requires Windows')
    class Blob(ctypes.Structure):
        _fields_=[('size',ctypes.c_ulong),('data',ctypes.POINTER(ctypes.c_ubyte))]
    def Input(value):
        buffer=(ctypes.c_ubyte*len(value)).from_buffer_copy(value)
        return buffer,Blob(len(value),buffer)
    buffer,source=Input(data);entropy_buffer,optional=Input(entropy);output=Blob()
    api=ctypes.WinDLL('crypt32',use_last_error=True)
    api.CryptProtectData.argtypes=[ctypes.POINTER(Blob),ctypes.c_wchar_p,ctypes.POINTER(Blob),ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(Blob)]
    api.CryptProtectData.restype=ctypes.c_int
    if not api.CryptProtectData(ctypes.byref(source),'Bazzalt game key',ctypes.byref(optional),None,None,1,ctypes.byref(output)):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel=ctypes.WinDLL('kernel32');kernel.LocalFree.argtypes=[ctypes.c_void_p]
    try:return ctypes.string_at(output.data,output.size)
    finally:
        ctypes.memset(buffer,0,len(data));kernel.LocalFree(output.data)


class SecurityContext:
    def __init__(self, settings, master=None):
        security=settings['security'];self.Encrypted=security['encryption']!='none'
        self.EncryptManifests=bool(security['encrypt_manifests'])
        self.Method=security['key_storage'] if self.Encrypted else 'none'
        self.Environment=security['key_environment'] if self.Method in ('environment','keychain') else ''
        self.KeyService=settings.get('platforms',{}).get('macos',{}).get('bundle_identifier','com.bazzalt.game')+'.game'
        self.BuildId=os.urandom(16);self.Salt=os.urandom(32)
        self.Master=master if self.Encrypted else None
        if self.Encrypted and (master is None or len(master)!=32):raise ValueError('Missing encryption master key')
        self.Root=os.urandom(32) if self.Encrypted else None
        self.PackageKey=Derive(self.Root,self.Salt,'packages') if self.Root else None
        self.FooterKey=master if self.Method=='embedded' else bytes(32)

    def ProtectManifest(self, payload, purpose):
        if not self.EncryptManifests:return payload
        return b'BZENC003'+Encrypt(Derive(self.Root,self.Salt,purpose),payload,self.BuildId+purpose.encode('ascii'))

    def Bootstrap(self, folder, player_hash, seal_hash):
        wrapped=Encrypt(Derive(self.Master,self.Salt,'wrapping'),self.Root,self.BuildId) if self.Root else b''
        protected=ProtectForCurrentUser(self.Master,self.BuildId) if self.Method=='dpapi_user' else b''
        if self.Method=='file':(folder/'game.key').write_bytes(self.Master)
        return json.dumps(dict(format_version=3,build_id=self.BuildId.hex(),salt=self.Salt.hex(),player_hash=player_hash,seal_hash=seal_hash,
            content_encrypted=self.Encrypted,manifests_encrypted=self.EncryptManifests,
            key_storage=self.Method,key_environment=self.Environment,
            **(dict(key_service=self.KeyService) if self.Method=='keychain' else {}),
            wrapped_key=wrapped.hex(),protected_key=protected.hex()),separators=(',',':')).encode('ascii')
