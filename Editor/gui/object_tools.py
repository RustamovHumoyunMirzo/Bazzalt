"""Editor-only object commands and safe numeric mode shortcuts."""
from math import sqrt
from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtWidgets import QApplication, QAbstractSpinBox, QLineEdit, QPlainTextEdit, QTextEdit
from .gizmos import Vec3
from .transform_units import TransformUnits,SnapValue,SnapRotation

ROOT="00000000-0000-0000-0000-000000000000"

def SelectionRoots(selected,parents):
    selected=list(dict.fromkeys(selected));values=set(selected);result=[]
    for entity in selected:
        current=parents.get(entity,"");seen={entity};child=False
        while current and current!=ROOT and current not in seen:
            if current in values:child=True;break
            seen.add(current);current=parents.get(current,"")
        if not child:result.append(entity)
    return result

def ViewRotation(eye,target):
    forward=Vec3(*target)-Vec3(*eye)
    if forward.Length()<1e-6:return None
    forward=forward.Normalized();right=forward.Cross(Vec3(0,1,0))
    if right.Length()<1e-6:right=forward.Cross(Vec3(0,0,1))
    right=right.Normalized();up=right.Cross(forward).Normalized()
    # Object local -Z points along the editor camera's forward direction.
    m=((right.X,up.X,-forward.X),(right.Y,up.Y,-forward.Y),(right.Z,up.Z,-forward.Z));trace=m[0][0]+m[1][1]+m[2][2]
    if trace>0:
        s=sqrt(trace+1)*2;return ((m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4)
    i=max(range(3),key=lambda axis:m[axis][axis]);j=(i+1)%3;k=(i+2)%3;s=sqrt(max(0,1+m[i][i]-m[j][j]-m[k][k]))*2
    if s<1e-6:return None
    q=[0.,0.,0.,0.];q[i]=s/4;q[j]=(m[i][j]+m[j][i])/s;q[k]=(m[i][k]+m[k][i])/s;q[3]=(m[k][j]-m[j][k])/s;return tuple(q)

class ObjectTools(QObject):
    def __init__(self,window):
        super().__init__(window);self.Window=window;self.Controller=window.Controller;self.Runtime=window.Runtime
        for action in window.Toolbar.ModeActions.values():window.MenuBar.ObjectModeMenu.addAction(action);window.addAction(action)
        window.MenuBar.ObjectCommandRequested.connect(self.Execute)
        for action in window.MenuBar.ObjectActions.values():window.addAction(action)
        window.MenuBar.ObjectMenu.aboutToShow.connect(self.RefreshActions)
        QApplication.instance().installEventFilter(self)
        if hasattr(window,"Hierarchy"):window.Hierarchy.Tree.itemSelectionChanged.connect(self.RefreshActions)
        for name in ("EntityPicked","EntitiesBoxSelected"):
            if hasattr(window.Scene.Surface,name):getattr(window.Scene.Surface,name).connect(lambda *_:self.RefreshActions())

    def Close(self):QApplication.instance().removeEventFilter(self)

    def eventFilter(self,watched,event):
        if event.type()!=QEvent.Type.ShortcutOverride:return False
        numeric=Qt.Key.Key_1<=event.key()<=Qt.Key.Key_4 and not event.modifiers()&~Qt.KeyboardModifier.KeypadModifier
        tool=event.key() in {Qt.Key.Key_P,Qt.Key.Key_V,Qt.Key.Key_A,Qt.Key.Key_G,Qt.Key.Key_J} and event.modifiers()==(Qt.KeyboardModifier.ControlModifier|Qt.KeyboardModifier.ShiftModifier)
        tool=tool or (event.key() in {Qt.Key.Key_R,Qt.Key.Key_S} and event.modifiers()==(Qt.KeyboardModifier.ControlModifier|Qt.KeyboardModifier.AltModifier))
        if not (numeric or tool):return False
        window=self.Window;focus=QApplication.focusWidget();surface=window.Scene.Surface
        editing=False;current=focus
        while current is not None:
            if isinstance(current,(QLineEdit,QTextEdit,QPlainTextEdit,QAbstractSpinBox)):editing=True;break
            current=current.parentWidget()
        playing=focus in (window.Output.Surface,window.Output._no_camera) and self.Runtime.IsPlaying() and not self.Runtime.IsPaused()
        if editing or playing or QApplication.activeModalWidget() or surface._navigating or surface._gizmo_drag is not None:
            event.accept();return True
        return False

    def _Select(self,values):
        values=list(dict.fromkeys(value for value in values if value and value!=ROOT and self.Runtime.IsEditorSelectable(value)))
        self.Controller.SelectEntities(values);self.Window.Hierarchy.SetSelectedData(values)

    def RefreshActions(self):
        selected=bool(self.Controller.SelectedEntities);surface=self.Window.Scene.Surface
        for key,action in self.Window.MenuBar.ObjectActions.items():
            action.setEnabled(key in {"select_all","invert"} or selected)
        self.Window.MenuBar.ObjectActions["place_cursor"].setEnabled(selected and surface._has_scene_pointer)

    def Execute(self,command):
        window=getattr(self,"Window",None);scene=getattr(window,"Scene",None);surface=getattr(scene,"Surface",None)
        if getattr(surface,"_navigating",False) is True or QApplication.activeModalWidget():return
        controller=self.Controller;runtime=self.Runtime;selected=list(controller.SelectedEntities);parents=runtime.EditorParents
        if command in {"select_all","invert"}:
            self._Select(entity["uuid"] for entity in runtime.Entities() if command=="select_all" or entity["uuid"] not in selected);return
        if command=="deselect_all":self._Select([]);return
        if command=="select_parent":self._Select(parents.get(value,"") for value in selected);return
        if command in {"select_children","select_descendants"}:
            found=[];front=set(selected)
            while front:
                children={value for value,parent in parents.items() if parent in front and value not in selected and value not in found};found.extend(sorted(children))
                if command=="select_children":break
                front=children
            self._Select(found);return
        if command=="select_siblings":
            scenes={value:(runtime.EntityDetails(value) or {}).get("scene_uuid","") for value in parents}
            groups={(parents.get(value,"") or ROOT,scenes.get(value,"")) for value in selected}
            self._Select(value for value,parent in parents.items() if (parent or ROOT,scenes[value]) in groups);return
        if command=="duplicate":controller.DuplicateHierarchyEntities(selected);return
        if command=="delete":controller.DeleteEntity(SelectionRoots(selected,parents));return
        roots=SelectionRoots([value for value in selected if runtime.IsEditorSelectable(value)],parents)
        details={value:runtime.EntityDetails(value) for value in roots};details={value:item for value,item in details.items() if item}
        if not details:return
        surface=self.Window.Scene.Surface;center=tuple(sum(item["position"][i] for item in details.values())/len(details) for i in range(3));target=None;rotation=None
        if command=="place_cursor":target=surface.PlacementPosition(self.Window.Scene.GridPlane.currentIndex(),roots)
        elif command=="view_center":target=tuple(surface._target)
        elif command=="center_origin":target=(0,0,0)
        elif command=="align_view":rotation=ViewRotation(surface._eye,surface._target)
        elif command=="project_grid":
            target=list(center);target[{0:2,1:1,2:0}[self.Window.Scene.GridPlane.currentIndex()]]=0
        if command in {"place_cursor","view_center","center_origin","project_grid"} and target is None:return
        if command=="align_view" and rotation is None:return
        label=self.Window.Localization.Translate("object."+command);controller.History.BeginTransforms(label,roots);changed=False
        try:
            for value,item in details.items():
                position=item["position"];orientation=item["rotation"];scale=item["scale"]
                if target is not None:position=tuple(position[i]+target[i]-center[i] for i in range(3))
                if command=="snap_grid":position=tuple(SnapValue(component,TransformUnits(self.Window.GetPreferences().get("transform"))["translation_step"]) for component in position)
                elif command=="snap_rotation":orientation=SnapRotation(orientation,TransformUnits(self.Window.GetPreferences().get("transform"))["rotation_step"])
                elif command=="snap_scale":scale=tuple(SnapValue(component,TransformUnits(self.Window.GetPreferences().get("transform"))["scale_step"]) for component in scale)
                elif command=="reset_rotation":orientation=(0,0,0,1)
                elif command=="reset_scale":scale=(1,1,1)
                elif rotation is not None:orientation=rotation
                if (tuple(position),tuple(orientation),tuple(scale))==(tuple(item["position"]),tuple(item["rotation"]),tuple(item["scale"])):continue
                changed=runtime.SetTransform(value,position,orientation,scale) or changed
        except Exception:controller.History.Cancel();raise
        if changed:
            controller.History.Commit();controller.SetDirty(True,[item.get("scene_uuid","") for item in details.values()]);controller._RefreshInspectorValues();controller._UpdateGizmo()
        else:controller.History.Cancel()
