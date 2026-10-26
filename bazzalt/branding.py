"""Shared shaped icon and wordmark; canonical SVGs live in assets/branding."""
from __future__ import annotations
from pathlib import Path
import re
from PySide6.QtCore import QByteArray,QSize,QRectF,Qt
from PySide6.QtGui import QColor,QIcon,QPainter,QPixmap
from PySide6.QtSvg import QSvgRenderer
from .resources import Package

def LogoPath()->Path|str:
    package=Package("branding");path=package.Path("BazzaltIcon.svg")
    return path if package.Compiled else Path(path)

def BrandPixmap(name:str,color:str,size:QSize)->QPixmap:
    data=Package("branding").ReadText(name)
    tint=QColor(color).name()
    data=re.sub(r'\b(fill|stroke)="(?!none\b|url\()[^"]+"',lambda match:f'{match.group(1)}="{tint}"',data)
    renderer=QSvgRenderer(QByteArray(data.encode("utf-8")))
    if not renderer.isValid():raise ValueError('Invalid branding SVG: '+name)
    pixmap=QPixmap(size);pixmap.fill(QColor(Qt.GlobalColor.transparent))
    fitted=renderer.defaultSize().scaled(size,Qt.AspectRatioMode.KeepAspectRatio)
    bounds=QRectF((size.width()-fitted.width())/2,(size.height()-fitted.height())/2,fitted.width(),fitted.height())
    painter=QPainter(pixmap);renderer.render(painter,bounds);painter.end();return pixmap

def LogoIcon(color:str="#eeeeee",size:int=128)->QIcon:
    return QIcon(BrandPixmap("BazzaltIcon.svg",color,QSize(size,size)))

def WordmarkPixmap(color:str="#eeeeee",width:int=128)->QPixmap:
    return BrandPixmap("BazzaltText.svg",color,QSize(width,max(1,round(width*133/1412))))

__all__=["LogoIcon","LogoPath","WordmarkPixmap"]
