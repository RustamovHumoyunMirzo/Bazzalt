"""Probe authoring and cancellable, transactional offline diffuse GI baking."""
from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QFormLayout, QLabel, QPushButton, QProgressBar, QGroupBox
from .widgets.fields import IntInput, FloatInput

class LightingDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Runtime=editor.Runtime;self.Tr=editor.Localization.Translate;self.Capturing=False
        self.setWindowTitle(self.Tr('lighting.title'));self.setWindowFlag(Qt.WindowType.WindowMinimizeButtonHint,False);self.resize(480,520)
        layout=QVBoxLayout(self);grid=QGroupBox(self.Tr('lighting.grid'));form=QFormLayout(grid)
        self.Count=IntInput(1,40,2);self.Extent=FloatInput(.01,10000,5);self.Radius=FloatInput(.01,10000,15)
        for name,field in (('count',self.Count),('extent',self.Extent),('radius',self.Radius)):form.addRow(self.Tr('lighting.'+name),field)
        self.Create=QPushButton(self.Tr('lighting.create'));self.CreateReflection=QPushButton(self.Tr('lighting.create_reflection'));self.AddLightmaps=QPushButton(self.Tr('lighting.add_lightmaps'));form.addRow(self.Create);form.addRow(self.CreateReflection);form.addRow(self.AddLightmaps);layout.addWidget(grid)
        bake=QGroupBox(self.Tr('lighting.settings'));form=QFormLayout(bake)
        self.Samples=IntInput(16,16384,256);self.Bounces=IntInput(0,4,1);self.Distance=FloatInput(.01,100000,1000);self.Bias=FloatInput(.00001,1,.002,5);self.Ambient=FloatInput(0,100000,0)
        for name,field in (('samples',self.Samples),('bounces',self.Bounces),('distance',self.Distance),('bias',self.Bias),('ambient',self.Ambient)):form.addRow(self.Tr('lighting.'+name),field)
        self.LightmapSamples=IntInput(1,4096,4);self.ReflectionSamples=IntInput(1,4096,32);self.TexelBudget=IntInput(1,8388608,1048576)
        for name,field in (('lightmap_samples',self.LightmapSamples),('reflection_samples',self.ReflectionSamples),('texel_budget',self.TexelBudget)):form.addRow(self.Tr('lighting.'+name),field)
        layout.addWidget(bake);self.Progress=QProgressBar();self.Progress.setRange(0,1000);layout.addWidget(self.Progress)
        row=QHBoxLayout();self.Bake=QPushButton(self.Tr('lighting.bake'));self.Cancel=QPushButton(self.Tr('lighting.cancel'));self.Clear=QPushButton(self.Tr('lighting.clear'));self.Cancel.setEnabled(False)
        for button in (self.Bake,self.Cancel,self.Clear):row.addWidget(button)
        layout.addLayout(row);self.Status=QLabel();self.Status.setWordWrap(True);layout.addWidget(self.Status)
        self.Timer=QTimer(self);self.Timer.setInterval(100);self.Timer.timeout.connect(self.Poll)
        self.Bake.clicked.connect(self.Begin);self.Cancel.clicked.connect(self.Stop);self.Create.clicked.connect(self.CreateGrid);self.CreateReflection.clicked.connect(self.CreateReflectionProbe);self.AddLightmaps.clicked.connect(self.AddSelectedLightmaps);self.Clear.clicked.connect(self.ClearBake)
        self.Runtime.ProjectChanging.connect(self.Stop)

    def Change(self,command,*args):
        def apply():
            result=self.Runtime.LightingCommand(command,*args)
            return result is not False
        if self.Editor.Controller._Mutate(self.Tr('lighting.title'),apply):
            self.Editor.Controller.SetDirty(True);self.Runtime.SceneChanged.emit();return True
        return False

    def SetRunning(self,running):
        self.Capturing=running;self.Cancel.setEnabled(running)
        for button in (self.Bake,self.Clear,self.Create,self.CreateReflection,self.AddLightmaps):button.setEnabled(not running)

    def CreateGrid(self):
        try:self.Change('create_light_probe_grid',self.Count.GetValue(),self.Extent.GetValue(),self.Radius.GetValue());self.Status.setText(self.Tr('lighting.grid_created'))
        except Exception as error:self.Status.setText(str(error))

    def CreateReflectionProbe(self):
        try:self.Change('create_reflection_probe');self.Status.setText(self.Tr('lighting.reflection_created'))
        except Exception as error:self.Status.setText(str(error))

    def AddSelectedLightmaps(self):
        try:self.Change('add_lightmaps',list(self.Editor.Controller.SelectedEntities));self.Status.setText(self.Tr('lighting.lightmaps_added'))
        except Exception as error:self.Status.setText(str(error))

    def Begin(self):
        try:
            ambient=self.Ambient.GetValue();self.Runtime.LightingCommand('begin_lighting_bake',self.Samples.GetValue(),self.Bounces.GetValue(),self.Distance.GetValue(),self.Bias.GetValue(),(ambient,ambient,ambient),self.LightmapSamples.GetValue(),self.ReflectionSamples.GetValue(),self.TexelBudget.GetValue())
            self.SetRunning(True);self.Progress.setValue(0);self.Status.setText(self.Tr('lighting.baking'));self.Timer.start()
        except Exception as error:self.Status.setText(str(error))

    def Poll(self):
        try:
            info=self.Runtime.LightingCommand('lighting_bake_info');self.Progress.setValue(int(info['progress']*1000))
            if info['status']==0:return
            self.Timer.stop();self.SetRunning(False)
            if info['status']==1:
                if self.Change('apply_lighting_bake'):self.Status.setText(self.Tr('lighting.complete'))
                else:self.Status.setText(self.Runtime.LightingCommand('lighting_bake_info')['error'])
            elif info['status']==3:self.Status.setText(self.Tr('lighting.cancelled'))
            else:self.Status.setText(info['error'])
        except Exception as error:self.Timer.stop();self.SetRunning(False);self.Status.setText(str(error))

    def Stop(self):
        if self.Capturing:
            try:self.Runtime.LightingCommand('cancel_lighting_bake')
            except Exception as error:self.Status.setText(str(error))
            self.Status.setText(self.Tr('lighting.cancelled'))
        self.Timer.stop();self.SetRunning(False)

    def ClearBake(self):
        try:self.Change('clear_lighting_bake');self.Progress.setValue(0);self.Status.setText(self.Tr('lighting.cleared'))
        except Exception as error:self.Status.setText(str(error))

    def done(self,result):self.Stop();super().done(result)
