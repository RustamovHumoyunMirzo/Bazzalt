"""Read-only source metadata and private environment cache discovery."""
from pathlib import Path
import re
import struct
from PySide6.QtGui import QImageReader

ENVIRONMENT_EXTENSIONS={".hdr",".exr",".ktx"}

def InspectEnvironment(path,asset):
    path=Path(path);width=height=0
    with path.open("rb") as stream:header=stream.read(1024*1024)
    if path.suffix.lower()==".hdr":
        match=re.search(rb"[+-]Y\s+(\d+)\s+[+-]X\s+(\d+)",header)
        if match:height,width=map(int,match.groups())
    elif path.suffix.lower()==".exr" and header[:4]==b"\x76\x2f\x31\x01":
        offset=8
        while offset<len(header) and header[offset]:
            end=header.find(b"\0",offset);type_end=header.find(b"\0",end+1)
            if end<0 or type_end<0 or type_end+5>len(header):break
            name=header[offset:end];size=struct.unpack_from("<I",header,type_end+1)[0];start=type_end+5
            if size>len(header)-start:break
            if name==b"dataWindow" and size==16:
                xmin,ymin,xmax,ymax=struct.unpack_from("<4i",header,start);width=xmax-xmin+1;height=ymax-ymin+1;break
            offset=start+size
    elif path.suffix.lower()==".ktx" and header[:12]==b"\xabKTX 11\xbb\r\n\x1a\n" and len(header)>=64:
        endian="<" if header[12:16]==b"\x01\x02\x03\x04" else ">"
        width,height=struct.unpack_from(endian+"2I",header,36)
    if width<=0 or height<=0:
        size=QImageReader(str(path)).size();width,height=max(0,size.width()),max(0,size.height())
    cache=Path(asset.get("cache",""));folder=Path(str(cache)+".environment")
    preview=folder/"preview"/cache.stem/"skybox.png"
    return {"width":width,"height":height,"preview":preview,
        "ibl":folder/(folder.stem+"_ibl.ktx"),"skybox":folder/(folder.stem+"_skybox.ktx"),
        "settings":dict(asset.get("settings",{}))}
