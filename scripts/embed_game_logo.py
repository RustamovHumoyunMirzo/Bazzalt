"""Rasterize the shared SVG at build time; shipped games do not need Qt/SVG."""
import argparse,os
from pathlib import Path
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide6.QtCore import QRectF,Qt
from PySide6.QtGui import QGuiApplication,QImage,QPainter,QFont,QColor
from PySide6.QtSvg import QSvgRenderer

def Generate(source,output):
    application=QGuiApplication.instance() or QGuiApplication([])
    renderer=QSvgRenderer(str(source))
    if not renderer.isValid():raise ValueError('Invalid Bazzalt logo SVG')
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    icon=output.suffix.lower()=='.ico'
    size=renderer.defaultSize();width=256;height=256 if icon else max(1,round(width*size.height()/size.width()))
    image=QImage(width,height,QImage.Format.Format_ARGB32 if icon else QImage.Format.Format_RGB32);image.fill(0 if icon else 0xff000000)
    scale=min(width/size.width(),height/size.height());w,h=size.width()*scale,size.height()*scale
    painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing);renderer.render(painter,QRectF((width-w)/2,(height-h)/2,w,h));painter.end()
    if icon:
        if not image.save(str(output),'ICO'):raise RuntimeError('Could not write application icon')
        return
    data=bytes(image.constBits());rows=[','.join(str(v) for v in data[i:i+32]) for i in range(0,len(data),32)]
    intro=QImage(width,height+64,QImage.Format.Format_RGB32);intro.fill(0xff000000);painter=QPainter(intro);painter.drawImage(0,0,image);painter.setPen(QColor('white'));painter.setFont(QFont('Sans Serif',14));painter.drawText(QRectF(0,height+8,width,48),Qt.AlignmentFlag.AlignCenter,'Powered by Bazzalt');painter.end()
    intro_data=bytes(intro.constBits());intro_rows=[','.join(str(v) for v in intro_data[i:i+32]) for i in range(0,len(intro_data),32)]
    output.write_text(f'#pragma once\ninline constexpr int GameLogoWidth={width},GameLogoHeight={height};\ninline constexpr unsigned char GameLogoPixels[]={{\n'+',\n'.join(rows)+'\n};\n'+f'inline constexpr int GameIntroWidth={width},GameIntroHeight={height+64};\ninline constexpr unsigned char GameIntroPixels[]={{\n'+',\n'.join(intro_rows)+'\n};\n',encoding='ascii')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();Generate(args.source,args.output)
