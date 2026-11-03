"""VFX flow authoring reuses the editor's shared themed graph canvas/history."""
import copy,json
from PySide6.QtCore import Qt,QRectF
from PySide6.QtGui import QImage,QPainter,QColor,QPixmap
from PySide6.QtWidgets import QLineEdit,QWidget,QHBoxLayout,QLabel
from .shader_graph import ShaderGraphPanel,Node
from ..widgets.fields import AssetPickerInput,FloatInput,IntInput,Vec3Input,Vec4Input,BoolInput,EnumInput,UIntInput
from ... import vfx_graph as Graph
class VFXGraphPanel(ShaderGraphPanel):
    Model=Graph
    OutputType='Output'
    IsVFX=True
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        for widget in (self.Shading,self.Blending,self.DoubleSided,self.Mesh,self.Buttons[-1]):widget.hide()
        self.PreviewTime=FloatInput(minimum=0,maximum=10,value=1.);self.PreviewTime.setToolTip(self.Tr('vfx.preview_time'));self.PreviewTime.valueChanged.connect(lambda _:self.Preview());row=QWidget();layout=QHBoxLayout(row);layout.setContentsMargins(0,0,0,0);self.TimeLabel=QLabel(self.Tr('vfx.preview_time'));layout.addWidget(self.TimeLabel);layout.addWidget(self.PreviewTime);self.Image.parentWidget().layout().insertWidget(0,row)
    def Tr(self,key):
        if key.startswith('graph.node.'):key=key.replace('graph.node.','vfx.node.',1)
        elif key.startswith('graph.category.'):key=key.replace('graph.category.','vfx.category.',1)
        elif key=='graph.empty':key='vfx.empty'
        return self.Locale.Translate(key)
    def SyncOptions(self):pass
    def Retranslate(self,*_):
        if hasattr(self,'TimeLabel'):self.TimeLabel.setText(self.Tr('vfx.preview_time'))
        for i in range(self.Palette.topLevelItemCount()):
            category=self.Palette.topLevelItem(i);category.setText(0,self.Tr('graph.category.'+category.data(0,Qt.ItemDataRole.UserRole+1)))
            for j in range(category.childCount()):item=category.child(j);item.setText(0,self.Tr('graph.node.'+item.data(0,Qt.ItemDataRole.UserRole)))
        self.Rebuild();self.Repaint()
    def AddImport(self,identity,point):
        def Op():Graph.AddNode(self.Graph,'Import',(point.x(),point.y()))['values']['asset']=identity
        self.Edit(Op)
    def Inspect(self):
        while self.Form.rowCount():
            row=self.Form.takeRow(0)
            for item in (row.labelItem,row.fieldItem):
                if item and item.widget():item.widget().hide();item.widget().deleteLater()
        selected=[n for n in self.Canvas.scene().selectedItems() if isinstance(n,Node)]
        if len(selected)!=1:return
        node=selected[0].Data
        for key,default in Graph.DEFAULTS[node['type']].items():
            value=node['values'].get(key,copy.deepcopy(default))
            def Commit(value,k=key,identity=node['id'],asset=isinstance(default,str)):
                if asset:value=value or '00000000-0000-0000-0000-000000000000'
                if isinstance(value,tuple):value=list(value)
                self.Edit(lambda:next(n for n in self.Graph['nodes'] if n['id']==identity)['values'].__setitem__(k,value))
            if (node['type']=='Collision' and key=='mode') or (node['type']=='MeshOutput' and key=='primitive'):
                editor=EnumInput();names=('world','plane') if key=='mode' else ('Cube','Sphere','Cylinder','Capsule','Plane','Cone','Torus');prefix='vfx.collision.' if key=='mode' else 'vfx.primitive.';editor.SetOptions([(self.Tr(prefix+name),i) for i,name in enumerate(names)]);editor.SetValue(value);editor.currentIndexChanged.connect(lambda _,e=editor,c=Commit:c(e.GetValue()))
            elif key=='layerMask':editor=UIntInput(value=value);editor.ValueChanged.connect(Commit)
            elif isinstance(default,str):
                extensions={'.vfx'} if key=='asset' else {'.glb','.gltf'} if key=='mesh' else {'.matinst'} if key=='material' else {'.png','.jpg','.jpeg','.bmp','.tga','.webp'}
                editor=AssetPickerInput(self.Tr('properties.select_project_asset'),accepted_extensions=extensions);editor.AssetResolver=getattr(self.Runtime,'ResolveAssetRecord',None);editor.ConfigureAssets(list(self.AssetProvider()));editor.SetValue(value);editor.PickRequested.connect(editor.OpenProjectPicker);editor.ValueChanged.connect(Commit)
            elif isinstance(value,bool):editor=BoolInput(value);editor.ValueChanged.connect(Commit)
            elif isinstance(value,int):editor=IntInput(minimum=int(Graph.RANGES.get(key,(0,1000000))[0]),maximum=int(Graph.RANGES.get(key,(0,1000000))[1]),value=value);editor.valueChanged.connect(Commit)
            elif isinstance(value,list):editor=(Vec3Input if len(value)==3 else Vec4Input)(value);editor.ValueChanged.connect(Commit)
            else:editor=FloatInput(minimum=Graph.RANGES.get(key,(-1000000,1000000))[0],maximum=Graph.RANGES.get(key,(-1000000,1000000))[1],value=value);editor.valueChanged.connect(Commit)
            self.Form.addRow(self.Tr('vfx.field.'+key),editor)
    def Preview(self):
        if not self.Path or not self.Flush():return
        try:
            from ...runtime import _LoadNativeModule
            module=_LoadNativeModule()
            if not module or not hasattr(module,'preview_vfx'):raise ValueError(self.Tr('vfx.native_required'))
            points=module.preview_vfx(Graph.Generate(self.Graph),getattr(self,'PreviewTime',None).value() if hasattr(self,'PreviewTime') else 1.)
            image=QImage(240,240,QImage.Format.Format_ARGB32_Premultiplied);image.fill(QColor(self.Themes.GetTheme().background));p=QPainter(image);p.setRenderHint(QPainter.RenderHint.Antialiasing);p.setPen(Qt.PenStyle.NoPen)
            for x,y,z,size,r,g,b,a in sorted(points,key=lambda row:row[2]):
                p.setBrush(QColor.fromRgbF(*[max(0.,min(1.,v)) for v in (r,g,b,a)]));radius=max(1.,min(50.,size*24));p.drawEllipse(QRectF(120+x*28-radius,160-y*28-radius,radius*2,radius*2))
            p.end();self.Image.SetBusy(False);self.Image.setPixmap(QPixmap.fromImage(image));self.Error.clear()
        except (OSError,ValueError,RuntimeError) as e:self.PreviewFailed(str(e))
    def Unlock(self):pass
