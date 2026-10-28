"""Physics authoring through the native scene API and shared editor widgets."""
from PySide6.QtCore import Qt,QSignalBlocker
from PySide6.QtWidgets import QDialog,QVBoxLayout,QDialogButtonBox,QTableWidget,QTableWidgetItem,QPushButton
from .widgets import BoolInput,EnumInput,FloatInput,IntInput,MultiSelectInput,Vec3Input

def Key(label):return label.lower().replace(' ','_')

def ComponentField(controller,entity,component,name,value):
    if component not in {'Rigidbody','Shape Collider','Mesh Collider'}:return None
    tr=controller.Window.Localization.Translate
    commit=lambda v:controller._CommitComponent(entity,component,name,v)
    enums={'Motion Type':('static','kinematic','dynamic'),'Collision Detection':('discrete','continuous'),'Interpolation':('none','interpolate'),'Shape':('box','sphere','capsule','cylinder','plane')}
    if name in enums:
        field=EnumInput();field.SetOptions([(tr('physics.'+key),i) for i,key in enumerate(enums[name])]);field.SetValue(value)
        field.currentIndexChanged.connect(lambda _:commit(field.GetValue()));return field
    if name=='Constraints':
        field=MultiSelectInput(tr('physics.none'));field.SetOptions([(tr('physics.constraint.'+key),1<<i) for i,key in enumerate(('position_x','position_y','position_z','rotation_x','rotation_y','rotation_z'))]);field.SetValues([1<<i for i in range(6) if value&(1<<i)])
        field.SelectionChanged.connect(lambda values:commit(sum(values)));return field
    if name=='Mesh Asset':
        field=controller._AssetPicker(value,{'.gltf','.glb'},lambda v:commit(v or '00000000-0000-0000-0000-000000000000'));field.Display.setPlaceholderText(tr('physics.attached_geometry'));return field
    if name=='Layer':
        field=EnumInput();field.SetOptions([(str(i),i) for i in range(32)]);field.SetValue(value);field.currentIndexChanged.connect(lambda _:commit(field.GetValue()));return field
    if isinstance(value,float):
        limits={'Mass':(.0001,1e12),'Inertia Multiplier':(.0001,1e6),'Linear Damping':(0,1e6),'Angular Damping':(0,1e6),'Restitution':(0,1),'Friction':(0,1e6),'Radius':(.001,1e6),'Height':(.001,1e6),'Convex Radius':(0,1e6),'Max Linear Velocity':(.001,1e6),'Max Angular Velocity':(.001,1e6),'Gravity Factor':(-1e6,1e6)}
        lo,hi=limits.get(name,(-1e6,1e6));field=FloatInput(minimum=lo,maximum=hi,value=value);field.valueChanged.connect(commit);return field
    if name in {'Velocity Iterations','Position Iterations'}:
        field=IntInput(minimum=0,maximum=255,value=value);field.valueChanged.connect(commit);return field
    return None

class CollisionLayersDialog(QDialog):
    def __init__(self,parent,translate,masks):
        super().__init__(parent);self.setWindowTitle(translate('physics.layers'));self.setModal(True);self.resize(900,650)
        self.Masks=list(masks);layout=QVBoxLayout(self);self.Table=QTableWidget(32,32,self);self.Table.setHorizontalHeaderLabels([str(i) for i in range(32)]);self.Table.setVerticalHeaderLabels([str(i) for i in range(32)])
        for row in range(32):
            self.Table.setRowHeight(row,23)
            for column in range(32):
                self.Table.setColumnWidth(column,25);item=QTableWidgetItem();item.setFlags(Qt.ItemFlag.ItemIsEnabled|Qt.ItemFlag.ItemIsUserCheckable);item.setCheckState(Qt.CheckState.Checked if masks[row]&(1<<column) and masks[column]&(1<<row) else Qt.CheckState.Unchecked);self.Table.setItem(row,column,item)
        self.Table.itemChanged.connect(self._Change);layout.addWidget(self.Table)
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel);buttons.accepted.connect(self.accept);buttons.rejected.connect(self.reject);layout.addWidget(buttons)
    def _Change(self,item):
        row,column=item.row(),item.column();checked=item.checkState()==Qt.CheckState.Checked
        for a,b in ((row,column),(column,row)):
            if checked:self.Masks[a]|=1<<b
            else:self.Masks[a]&=~(1<<b)
        blocker=QSignalBlocker(self.Table);self.Table.item(column,row).setCheckState(item.checkState());del blocker

def InspectScene(controller,scene_id):
    runtime=controller.Runtime;data=getattr(runtime,'ScenePhysics',lambda _:{})(scene_id)
    if not data:return
    tr=controller.Window.Localization.Translate;section=controller.Window.Properties.AddComponentSection('scene.physics',tr('physics.section'),removable=False)
    def commit(key,value):
        active=scene_id==str(runtime.SceneInfo().get('uuid'))
        if active:controller.History.Begin(tr('physics.history'))
        if runtime.SetScenePhysics(scene_id,{key:value}):
            if active:controller.History.Commit()
            controller.SetDirty(True,[scene_id])
        else:
            if active:controller.History.Cancel()
            controller.Window.Console.AddMessage(tr('physics.invalid_settings'),source='Physics')
    bounds={'Max Bodies':(1,1048576),'Max Body Pairs':(1,4194304),'Max Contact Constraints':(1,1048576),'Collision Steps':(1,16),'Velocity Iterations':(1,255),'Position Iterations':(1,255),'Worker Threads':(0,32)}
    for key,value in data.items():
        if key=='Collision Masks':
            field=QPushButton(tr('physics.edit_layers'))
            def edit(_=False,masks=value):
                dialog=CollisionLayersDialog(controller.Window,tr,masks)
                if dialog.exec():commit('Collision Masks',dialog.Masks);masks[:]=dialog.Masks
                dialog.deleteLater()
            field.clicked.connect(edit)
        elif isinstance(value,bool):field=BoolInput(value);field.ValueChanged.connect(lambda v,k=key:commit(k,v))
        elif isinstance(value,int):
            lo,hi=bounds.get(key,(0,1048576));field=IntInput(minimum=lo,maximum=hi,value=value);field.valueChanged.connect(lambda v,k=key:commit(k,v))
        elif isinstance(value,float):field=FloatInput(minimum=0,maximum=1 if key=='Baumgarte' else 1000000,value=value);field.valueChanged.connect(lambda v,k=key:commit(k,v))
        elif isinstance(value,(list,tuple)) and len(value)==3:field=Vec3Input(value);field.ValueChanged.connect(lambda v,k=key:commit(k,v))
        else:continue
        section.AddField(tr('physics.field.'+Key(key)),field)
