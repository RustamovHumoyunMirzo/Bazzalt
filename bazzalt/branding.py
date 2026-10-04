"""Shared BAZZALT branding sourced from Launcher/BazzaltLogo.svg."""
from __future__ import annotations
from pathlib import Path
from PySide6.QtCore import QByteArray,QSize,Qt
from PySide6.QtGui import QColor,QIcon,QPainter,QPixmap
from PySide6.QtSvg import QSvgRenderer
from .resources import Package

def LogoPath()->Path|str:
    package=Package("branding");path=package.Path("BazzaltLogo.svg")
    return path if package.Compiled else Path(path)

def LogoIcon(color:str="#eeeeee",size:int=128)->QIcon:
    data=Package("branding").ReadText("BazzaltLogo.svg").replace('stroke="white"',f'stroke="{QColor(color).name()}"')
    renderer=QSvgRenderer(QByteArray(data.encode("utf-8")));pixmap=QPixmap(QSize(size,size));pixmap.fill(QColor(Qt.GlobalColor.transparent))
    painter=QPainter(pixmap);renderer.render(painter);painter.end();return QIcon(pixmap)

__all__=["LogoIcon","LogoPath"]
