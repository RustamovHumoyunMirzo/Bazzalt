"""Shared BAZZALT branding sourced from Launcher/BazzaltLogo.svg."""
from __future__ import annotations
import sys
from pathlib import Path
from PySide6.QtCore import QByteArray,QSize,Qt
from PySide6.QtGui import QColor,QIcon,QPainter,QPixmap
from PySide6.QtSvg import QSvgRenderer

def LogoPath()->Path:
    roots=(Path(__file__).resolve().parent.parent,Path(sys.executable).resolve().parent)
    for root in roots:
        candidate=root/"Launcher"/"BazzaltLogo.svg"
        if candidate.is_file():return candidate
    return roots[0]/"Launcher"/"BazzaltLogo.svg"

def LogoIcon(color:str="#eeeeee",size:int=128)->QIcon:
    path=LogoPath()
    if not path.is_file():return QIcon()
    data=path.read_text(encoding="utf-8").replace('stroke="white"',f'stroke="{QColor(color).name()}"')
    renderer=QSvgRenderer(QByteArray(data.encode("utf-8")));pixmap=QPixmap(QSize(size,size));pixmap.fill(QColor(Qt.GlobalColor.transparent))
    painter=QPainter(pixmap);renderer.render(painter);painter.end();return QIcon(pixmap)

__all__=["LogoIcon","LogoPath"]
