import sys
from pathlib import Path
from PySide6.QtCore import QByteArray, QRectF, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

app=QGuiApplication.instance() or QGuiApplication([])
source,output,size=Path(sys.argv[1]),Path(sys.argv[2]),int(sys.argv[3])
renderer=QSvgRenderer(QByteArray(source.read_bytes()))
if not renderer.isValid():raise SystemExit(f"Invalid SVG: {source}")
image=QImage(size,size,QImage.Format.Format_RGBA8888);image.fill(Qt.GlobalColor.transparent)
painter=QPainter(image);renderer.render(painter,QRectF(0,0,size,size));painter.end()
output.parent.mkdir(parents=True,exist_ok=True)
bits=image.constBits();output.write_bytes(bytes(bits[:image.sizeInBytes()]))
