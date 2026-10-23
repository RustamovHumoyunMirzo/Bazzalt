"""Write Windows version resources before encryption overlays or code signing."""
import ctypes
from ctypes import wintypes
import re
import struct

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
