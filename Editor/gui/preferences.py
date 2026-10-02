"""Modal, persistent editor preferences."""
from __future__ import annotations
from copy import deepcopy
from .widgets.fields import RangeInput
from PySide6.QtWidgets import QScrollArea
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox,QComboBox,QDialog,QDoubleSpinBox,QFormLayout,QHBoxLayout,QLabel,QListWidget,QPushButton,QStackedWidget,QVBoxLayout,QWidget

DEFAULT_PREFERENCES={"general":{"confirm_unsaved":True,"save_workspace":True},"appearance":{"theme":"dark","locale":"en"},"scene":{"navigation_speed":5.0,"grid_visible":True,"grid_plane":1},"console":{"clear_on_play":False},"scripting":{"show_compile_success":True}}
DEFAULT_PREFERENCES["scene"].update(pivot_center=False,local_space=False,gizmos_visible=True,stats_visible=False,icons_visible=True,shading_mode=0,look_sensitivity=.35,fly_boost=3.0)
DEFAULT_PREFERENCES["scene"]["orientation_visible"]=True
DEFAULT_PREFERENCES["history"]={"command_limit":100,"memory_mb":128}
DEFAULT_PREFERENCES["rendering"]={"backend":"automatic"}

def MergePreferences(value)->dict:
    result=deepcopy(DEFAULT_PREFERENCES)
    if isinstance(value,dict):
        for section,fields in value.items():
            if section in result and isinstance(fields,dict):result[section].update({key:item for key,item in fields.items() if key in result[section]})
    return result

class PreferencesDialog(QDialog):
    def __init__(self,editor)->None:
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate
        self.setObjectName("PreferencesDialog");self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.setWindowTitle(self.Tr("preferences.title"));self.resize(680,460);self.setMinimumSize(560,380)
        root=QVBoxLayout(self);root.setContentsMargins(10,10,10,10);root.setSpacing(10);body=QHBoxLayout();body.setSpacing(10);self.Sections=QListWidget();self.Sections.setObjectName("PreferencesSections");self.Sections.setFixedWidth(155);self.Pages=QStackedWidget();body.addWidget(self.Sections);body.addWidget(self.Pages,1);root.addLayout(body,1)
        self.Controls={};self._AddGeneral();self._AddAppearance();self._AddScene();self._AddConsole();self._AddScripting();self._AddHistory();self._AddRendering();self.Sections.currentRowChanged.connect(self.Pages.setCurrentIndex);self.Sections.setCurrentRow(0)
        buttons=QHBoxLayout();self.Restore=QPushButton(self.Tr("preferences.restore_defaults"));self.Cancel=QPushButton(self.Tr("preferences.cancel"));self.Apply=QPushButton(self.Tr("preferences.apply"));self.Apply.setDefault(True);buttons.addWidget(self.Restore);buttons.addStretch();buttons.addWidget(self.Cancel);buttons.addWidget(self.Apply);root.addLayout(buttons)
        self.Restore.clicked.connect(lambda:self.SetValues(DEFAULT_PREFERENCES));self.Cancel.clicked.connect(self.reject);self.Apply.clicked.connect(self._Apply);self.SetValues(editor.GetPreferences())
    def _Page(self,key:str)->QFormLayout:
        self.Sections.addItem(self.Tr(f"preferences.section.{key}"));page=QWidget();layout=QFormLayout(page);layout.setContentsMargins(14,12,14,12);layout.setHorizontalSpacing(20);layout.setVerticalSpacing(10);layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow);scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);self.Pages.addWidget(scroll);return layout
    def _AddGeneral(self):
        layout=self._Page("general");confirm=QCheckBox(self.Tr("preferences.confirm_unsaved"));workspace=QCheckBox(self.Tr("preferences.save_workspace"));layout.addRow(confirm);layout.addRow(workspace);self.Controls.update(confirm_unsaved=confirm,save_workspace=workspace)
    def _AddAppearance(self):
        layout=self._Page("appearance");theme=QComboBox();theme.addItem(self.Tr("action.theme_dark"),"dark");theme.addItem(self.Tr("action.theme_light"),"light");locale=QComboBox();locale.addItem(self.Tr("preferences.language_english"),"en");layout.addRow(self.Tr("preferences.theme"),theme);layout.addRow(self.Tr("preferences.language"),locale);self.Controls.update(theme=theme,locale=locale)
    def _AddScene(self):
        layout=self._Page("scene");speed=QDoubleSpinBox();speed.setRange(.1,100.0);speed.setDecimals(1);speed.setSuffix("×");grid=QCheckBox(self.Tr("preferences.grid_visible"));plane=QComboBox();plane.addItem("XY",0);plane.addItem("XZ",1);plane.addItem("YZ",2);layout.addRow(self.Tr("preferences.navigation_speed"),speed);layout.addRow(grid);layout.addRow(self.Tr("preferences.grid_plane"),plane);self.Controls.update(navigation_speed=speed,grid_visible=grid,grid_plane=plane)
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible"):
            field=QCheckBox(self.Tr(f"preferences.{key}"));layout.addRow(field);self.Controls[key]=field
        shading=QComboBox()
        for index,name in enumerate(("Lit","Unlit","Wireframe","Lighting Only","Overdraw")):shading.addItem(name,index)
        layout.addRow(self.Tr("preferences.shading_mode"),shading);self.Controls["shading_mode"]=shading
        for key,minimum,maximum,value in (("look_sensitivity",.05,2,.35),("fly_boost",1,10,3)):
            field=RangeInput(minimum,maximum,value,decimals=2);layout.addRow(self.Tr(f"preferences.{key}"),field);self.Controls[key]=field
    def _AddConsole(self):
        layout=self._Page("console");clear=QCheckBox(self.Tr("preferences.clear_console_play"));layout.addRow(clear);self.Controls["clear_on_play"]=clear
    def _AddHistory(self):
        layout=self._Page("history")
        for key,minimum,maximum,value in (("command_limit",1,1000,100),("memory_mb",1,2048,128)):
            field=RangeInput(minimum,maximum,value,decimals=0);layout.addRow(self.Tr(f"preferences.{key}"),field);self.Controls[key]=field
    def _AddScripting(self):
        layout=self._Page("scripting");success=QCheckBox(self.Tr("preferences.show_compile_success"));layout.addRow(success);note=QLabel(self.Tr("preferences.compiler_note"));note.setWordWrap(True);note.setObjectName("PreferencesHint");layout.addRow(note);self.Controls["show_compile_success"]=success
    def _AddRendering(self):
        layout=self._Page("rendering");backend=QComboBox()
        for name in self.Editor.Runtime.SupportedRenderingBackends():
            backend.addItem(self.Tr("preferences.backend."+name),name)
        layout.addRow(self.Tr("preferences.rendering_backend"),backend)
        note=QLabel(self.Tr("preferences.rendering_restart_note"));note.setWordWrap(True);layout.addRow(note)
        self.Controls["rendering_backend"]=backend
    @staticmethod
    def _ComboSet(combo,value):index=combo.findData(value);combo.setCurrentIndex(max(0,index))
    def SetValues(self,value:dict)->None:
        p=MergePreferences(value);c=self.Controls;c["confirm_unsaved"].setChecked(bool(p["general"]["confirm_unsaved"]));c["save_workspace"].setChecked(bool(p["general"]["save_workspace"]));self._ComboSet(c["theme"],p["appearance"]["theme"]);self._ComboSet(c["locale"],p["appearance"]["locale"]);c["navigation_speed"].setValue(float(p["scene"]["navigation_speed"]));c["grid_visible"].setChecked(bool(p["scene"]["grid_visible"]));self._ComboSet(c["grid_plane"],int(p["scene"]["grid_plane"]));c["clear_on_play"].setChecked(bool(p["console"]["clear_on_play"]));c["show_compile_success"].setChecked(bool(p["scripting"]["show_compile_success"]))
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible"):c[key].setChecked(bool(p["scene"][key]))
        self._ComboSet(c["shading_mode"],p["scene"]["shading_mode"])
        for key in ("look_sensitivity","fly_boost"):c[key].SetValue(p["scene"][key])
        for key in ("command_limit","memory_mb"):c[key].SetValue(p["history"][key])
        self._ComboSet(c["rendering_backend"],p["rendering"]["backend"])
    def Values(self)->dict:
        c=self.Controls;result=MergePreferences(self.Editor.GetPreferences())
        result.update(general={"confirm_unsaved":c["confirm_unsaved"].isChecked(),"save_workspace":c["save_workspace"].isChecked()},appearance={"theme":c["theme"].currentData(),"locale":c["locale"].currentData()},console={"clear_on_play":c["clear_on_play"].isChecked()},scripting={"show_compile_success":c["show_compile_success"].isChecked()})
        result["scene"].update(navigation_speed=c["navigation_speed"].value(),grid_visible=c["grid_visible"].isChecked(),grid_plane=c["grid_plane"].currentData(),shading_mode=c["shading_mode"].currentData())
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible"):result["scene"][key]=c[key].isChecked()
        for key in ("look_sensitivity","fly_boost"):result["scene"][key]=c[key].GetValue()
        result["history"]={key:int(c[key].GetValue()) for key in ("command_limit","memory_mb")}
        result["rendering"]={"backend":c["rendering_backend"].currentData() or "automatic"}
        return result
    def _Apply(self)->None:self.Editor.ApplyPreferences(self.Values());self.accept()

__all__=["DEFAULT_PREFERENCES","MergePreferences","PreferencesDialog"]
