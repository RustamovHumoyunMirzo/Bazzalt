"""Rasterize the shared SVG at build time; shipped games do not need Qt/SVG."""
import argparse,os
from pathlib import Path
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide6.QtCore import QRectF
from PySide6.QtGui import QGuiApplication,QImage,QPainter
from PySide6.QtSvg import QSvgRenderer

def Generate(source,output):
    application=QGuiApplication.instance() or QGuiApplication([])
    renderer=QSvgRenderer(str(source))
    if not renderer.isValid():raise ValueError('Invalid Bazzalt logo SVG')
    width,height=256,292;image=QImage(width,height,QImage.Format.Format_RGB32);image.fill(0xff000000)
    painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing);renderer.render(painter,QRectF(0,0,width,height));painter.end()
    data=bytes(image.constBits());rows=[','.join(str(v) for v in data[i:i+32]) for i in range(0,len(data),32)]
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('#pragma once\ninline constexpr int GameLogoWidth=256,GameLogoHeight=292;\ninline constexpr unsigned char GameLogoPixels[]={\n'+',\n'.join(rows)+'\n};\n',encoding='ascii')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();Generate(args.source,args.output)
