"""Write Windows version resources before encryption overlays or code signing."""
import ctypes
from ctypes import wintypes
import re
import struct

def StripTemplateSignature(executable):
    """Changing production resources invalidates a template's old signature.

    Remove its certificate table before appending the game's new seal; the
    publisher must sign the final game, not reuse the template's signature.
    """
    from pathlib import Path
    path=Path(executable);data=bytearray(path.read_bytes())
    if len(data)<64 or data[:2]!=b'MZ':raise ValueError('Invalid player template')
    pe=struct.unpack_from('<I',data,60)[0]
    if pe+24+152>len(data) or data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid player template PE header')
    optional=pe+24;magic=struct.unpack_from('<H',data,optional)[0]
    if magic not in (0x10b,0x20b):raise ValueError('Unsupported player template format')
    certificate=optional+(96 if magic==0x10b else 112)+32
    offset,size=struct.unpack_from('<II',data,certificate)
    if offset or size:
        if offset<certificate+8 or size<8 or offset+size!=len(data):raise ValueError('Invalid template certificate table')
        del data[offset:];struct.pack_into('<II',data,certificate,0,0)
        struct.pack_into('<I',data,optional+64,0)
        path.write_bytes(data)

def Block(name,value=b'',children=(),text=False,length=None):
    key=name.encode('utf-16le')+b'\0\0';head=struct.pack('<HHH',0,len(value)//2 if text else len(value) if length is None else length,int(text))+key
    result=head+b'\0'*((-len(head))%4)+value
    for child in children:result+=b'\0'*((-len(result))%4)+child
    if len(result)>65535:raise ValueError('Executable version metadata is too large')
    return struct.pack('<H',len(result))+result[2:]
def VersionResource(production):
    version=production['version']
    if not re.fullmatch(r'\d+(?:\.\d+){1,3}',version):raise ValueError('Product version must contain 2 to 4 numeric parts')
    numbers=[int(value) for value in version.split('.')];numbers+=([0]*(4-len(numbers)))
    if any(value>65535 for value in numbers):raise ValueError('Product version part exceeds 65535')
    high=(numbers[0]<<16)|numbers[1];low=(numbers[2]<<16)|numbers[3]
    fixed=struct.pack('<13I',0xFEEF04BD,0x00010000,high,low,high,low,0x3f,0,0x40004,1,0,0,0)
    strings={'CompanyName':production['company'],'FileDescription':production['description'] or production['name'],'FileVersion':version,'ProductName':production['name'],'ProductVersion':version,'LegalCopyright':production['copyright'],'OriginalFilename':production['name']+'.exe'}
    table=Block('040904B0',children=[Block(name,text.encode('utf-16le')+b'\0\0',text=True) for name,text in strings.items()],text=True)
    info=Block('StringFileInfo',children=[table],text=True);variables=Block('VarFileInfo',children=[Block('Translation',struct.pack('<HH',0x409,1200))],text=True)
    return Block('VS_VERSION_INFO',fixed,[info,variables])
def Apply(executable,production):
    StripTemplateSignature(executable)
    import os
    if os.name!='nt':return ApplyPortable(executable,production)
    data=VersionResource(production);api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.BeginUpdateResourceW.argtypes=[wintypes.LPCWSTR,wintypes.BOOL];api.BeginUpdateResourceW.restype=wintypes.HANDLE
    api.UpdateResourceW.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p,wintypes.WORD,ctypes.c_void_p,wintypes.DWORD];api.UpdateResourceW.restype=wintypes.BOOL
    api.EndUpdateResourceW.argtypes=[wintypes.HANDLE,wintypes.BOOL];api.EndUpdateResourceW.restype=wintypes.BOOL
    handle=api.BeginUpdateResourceW(str(executable),False)
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    buffer=ctypes.create_string_buffer(data)
    if not api.UpdateResourceW(handle,ctypes.c_void_p(16),ctypes.c_void_p(1),0x409,buffer,len(data)):
        error=ctypes.get_last_error();api.EndUpdateResourceW(handle,True);raise ctypes.WinError(error)
    if not api.EndUpdateResourceW(handle,False):raise ctypes.WinError(ctypes.get_last_error())


def ApplyPortable(executable,production):
    """Preserve the resource tree and replace VERSIONINFO using LIEF on cross hosts."""
    import lief
    from pathlib import Path
    executable=Path(executable);StripTemplateSignature(executable);binary=lief.PE.parse(str(executable))
    if binary is None:raise ValueError('Invalid Windows player template')
    root=binary.resources if binary.has_resources else lief.PE.ResourceDirectory()
    if any(node.id==16 for node in root.childs):root.delete_child(16)
    version=lief.PE.ResourceDirectory(16);name=lief.PE.ResourceDirectory(1);value=lief.PE.ResourceData(list(VersionResource(production)));value.id=0x409
    name.add_child(value);version.add_child(name);root.add_child(version);binary.set_resources(root)
    config=lief.PE.Builder.config_t();config.resources=True;config.relocations=False;config.load_configuration=False;config.tls=False;config.overlay=True
    temporary=executable.with_suffix('.resource.tmp');binary.write(str(temporary),config)
    if not temporary.is_file():raise ValueError('Could not write Windows production resources')
    temporary.replace(executable)
