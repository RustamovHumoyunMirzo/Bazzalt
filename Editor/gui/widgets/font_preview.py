"""FreeType-backed project preview; never installs a font in the operating system."""
from PySide6.QtCore import Qt,QTimer
from PySide6.QtGui import QImage,QPixmap,QPainter,QPalette
from PySide6.QtWidgets import QWidget,QVBoxLayout,QLabel,QSizePolicy
from .fields import StringInput,FloatInput,IntInput

class FontPreview(QWidget):
    def __init__(self,path,localization,runtime,parent=None,faces=1):
        super().__init__(parent);self.setObjectName("FontPreview");self.setMinimumHeight(170)
        self.Path=path;self.Runtime=runtime
        layout=QVBoxLayout(self);layout.setContentsMargins(8,8,8,8);layout.setSpacing(6)
        tr=localization.Translate;self.Text=StringInput(tr("fonts.preview_text"));self.Size=FloatInput(minimum=8,maximum=128,value=32)
        self.Face=IntInput(minimum=0,maximum=max(0,int(faces)-1),value=0);self.Face.setToolTip(tr("fonts.face_index"));self.Face.setVisible(faces>1)
        self.Sample=QLabel(self);self.Sample.setObjectName("InspectorFontSample");self.Sample.setAlignment(Qt.AlignmentFlag.AlignLeft|Qt.AlignmentFlag.AlignVCenter);self.Sample.setMinimumHeight(192)
        self.Sample.setMinimumWidth(0);self.Sample.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Fixed)
        self.Timer=QTimer(self);self.Timer.setSingleShot(True);self.Timer.setInterval(120);self.Timer.timeout.connect(self._Update)
        layout.addWidget(self.Text);layout.addWidget(self.Size);layout.addWidget(self.Face);layout.addWidget(self.Sample,1)
        self.Text.textChanged.connect(lambda *_:self.Timer.start());self.Size.valueChanged.connect(lambda *_:self.Timer.start());self.Face.valueChanged.connect(lambda *_:self.Timer.start());self._Update()
    def _Update(self,*_):
        data=self.Runtime.FontPreview(self.Path,self.Text.text(),self.Size.value(),self.Face.value(),max(64,self.width()-16))
        if not data:self.Sample.clear();return
        image=QImage(data["pixels"],data["width"],data["height"],data["width"]*4,QImage.Format.Format_RGBA8888).copy()
        painter=QPainter(image);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(image.rect(),self.palette().color(QPalette.ColorRole.Text));painter.end();self.Sample.setPixmap(QPixmap.fromImage(image))
    def resizeEvent(self,event):
        super().resizeEvent(event)
        if hasattr(self,"Timer"):self.Timer.start()
    def changeEvent(self,event):
        super().changeEvent(event)
        if hasattr(self,"Timer"):self.Timer.start()
