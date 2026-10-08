"""Modal, persistent editor preferences."""
from __future__ import annotations
from copy import deepcopy
from .widgets.fields import RangeInput
from PySide6.QtWidgets import QScrollArea, QLineEdit, QFileDialog
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox,QComboBox,QDialog,QDoubleSpinBox,QFormLayout,QHBoxLayout,QLabel,QListWidget,QPushButton,QStackedWidget,QVBoxLayout,QWidget,QTreeWidget,QTreeWidgetItem,QMessageBox
import sys
from pathlib import Path
from ..platform_services import ChooseApplication
from .transform_units import DEFAULT_UNITS,TransformUnits

DEFAULT_PREFERENCES={"general":{"confirm_unsaved":True,"save_workspace":True},"appearance":{"theme":"dark","locale":"en"},"scene":{"navigation_speed":5.0,"grid_visible":True,"grid_plane":1},"console":{"clear_on_play":False},"scripting":{"show_compile_success":True}}
DEFAULT_PREFERENCES["scene"].update(pivot_center=False,local_space=False,gizmos_visible=True,stats_visible=False,icons_visible=True,shading_mode=0,look_sensitivity=.35,fly_boost=3.0)
DEFAULT_PREFERENCES["scene"]["orientation_visible"]=True
DEFAULT_PREFERENCES["scene"]["high_level_selection"]=False
DEFAULT_PREFERENCES["general"]["expand_new_hierarchy_items"]=False
DEFAULT_PREFERENCES["history"]={"command_limit":100,"memory_mb":128}
DEFAULT_PREFERENCES["rendering"]={"backend":"automatic"}
DEFAULT_PREFERENCES["file_associations"]={"remember":True}
DEFAULT_PREFERENCES["tools"]={"compiler_path":"","sdk_path":""}
DEFAULT_PREFERENCES["transform"]=dict(DEFAULT_UNITS)
STATISTIC_FIELDS=("fps","frame_ms","tick_ms","objects","selected","components","cameras","lights","renderables","render_primitives","viewports","captures","gui_batches")
DEFAULT_PREFERENCES["statistics"]={key:key in {"fps","objects"} for key in STATISTIC_FIELDS}

def MergePreferences(value)->dict:
    result=deepcopy(DEFAULT_PREFERENCES)
    if isinstance(value,dict):
        for section,fields in value.items():
            if section in result and isinstance(fields,dict):result[section].update({key:item for key,item in fields.items() if key in result[section]})
    result["transform"]=TransformUnits(result["transform"])
    return result

class PreferencesDialog(QDialog):
    def __init__(self,editor)->None:
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate
        self.setObjectName("PreferencesDialog");self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.setWindowTitle(self.Tr("preferences.title"));self.resize(680,460);self.setMinimumSize(560,380)
        root=QVBoxLayout(self);root.setContentsMargins(10,10,10,10);root.setSpacing(10);body=QHBoxLayout();body.setSpacing(10);self.Sections=QListWidget();self.Sections.setObjectName("PreferencesSections");self.Sections.setFixedWidth(155);self.Pages=QStackedWidget();body.addWidget(self.Sections);body.addWidget(self.Pages,1);root.addLayout(body,1)
        self._associations=editor.AssetBrowser.ExternalOpener.Associations()
        self.Controls={};self._AddGeneral();self._AddAppearance();self._AddScene();self._AddTransform();self._AddStatistics();self._AddConsole();self._AddScripting();self._AddTools();self._AddHistory();self._AddRendering();self._AddFileAssociations();self.Sections.currentRowChanged.connect(self.Pages.setCurrentIndex);self.Sections.setCurrentRow(0)
        buttons=QHBoxLayout();self.Restore=QPushButton(self.Tr("preferences.restore_defaults"));self.Cancel=QPushButton(self.Tr("preferences.cancel"));self.Apply=QPushButton(self.Tr("preferences.apply"));self.Apply.setDefault(True);buttons.addWidget(self.Restore);buttons.addStretch();buttons.addWidget(self.Cancel);buttons.addWidget(self.Apply);root.addLayout(buttons)
        self.Restore.clicked.connect(self._RestoreDefaults);self.Cancel.clicked.connect(self.reject);self.Apply.clicked.connect(self._Apply);self.SetValues(editor.GetPreferences())
    def _Page(self,key:str)->QFormLayout:
        self.Sections.addItem(self.Tr(f"preferences.section.{key}"));page=QWidget();layout=QFormLayout(page);layout.setContentsMargins(14,12,14,12);layout.setHorizontalSpacing(20);layout.setVerticalSpacing(10);layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow);scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);self.Pages.addWidget(scroll);return layout
    def _AddGeneral(self):
        layout=self._Page("general");confirm=QCheckBox(self.Tr("preferences.confirm_unsaved"));workspace=QCheckBox(self.Tr("preferences.save_workspace"));layout.addRow(confirm);layout.addRow(workspace);self.Controls.update(confirm_unsaved=confirm,save_workspace=workspace)
        expand=QCheckBox(self.Tr("preferences.expand_new_hierarchy_items"));layout.addRow(expand);self.Controls["expand_new_hierarchy_items"]=expand
    def _AddAppearance(self):
        layout=self._Page("appearance");theme=QComboBox();theme.addItem(self.Tr("action.theme_dark"),"dark");theme.addItem(self.Tr("action.theme_light"),"light");locale=QComboBox();locale.addItem(self.Tr("preferences.language_english"),"en");layout.addRow(self.Tr("preferences.theme"),theme);layout.addRow(self.Tr("preferences.language"),locale);self.Controls.update(theme=theme,locale=locale)
    def _AddScene(self):
        layout=self._Page("scene");speed=QDoubleSpinBox();speed.setRange(.1,100.0);speed.setDecimals(1);speed.setSuffix("×");grid=QCheckBox(self.Tr("preferences.grid_visible"));plane=QComboBox();plane.addItem("XY",0);plane.addItem("XZ",1);plane.addItem("YZ",2);layout.addRow(self.Tr("preferences.navigation_speed"),speed);layout.addRow(grid);layout.addRow(self.Tr("preferences.grid_plane"),plane);self.Controls.update(navigation_speed=speed,grid_visible=grid,grid_plane=plane)
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible","high_level_selection"):
            field=QCheckBox(self.Tr(f"preferences.{key}"));layout.addRow(field);self.Controls[key]=field
        shading=QComboBox()
        for index,name in enumerate(("lit","unlit","wireframe","lighting_only","overdraw")):shading.addItem(self.Tr("view.shading."+name),index)
        layout.addRow(self.Tr("preferences.shading_mode"),shading);self.Controls["shading_mode"]=shading
        for key,minimum,maximum,value in (("look_sensitivity",.05,2,.35),("fly_boost",1,10,3)):
            field=RangeInput(minimum,maximum,value,decimals=2);layout.addRow(self.Tr(f"preferences.{key}"),field);self.Controls[key]=field
    def _AddConsole(self):
        layout=self._Page("console");clear=QCheckBox(self.Tr("preferences.clear_console_play"));layout.addRow(clear);self.Controls["clear_on_play"]=clear
    def _AddTransform(self):
        layout=self._Page("transform")
        for key,value in DEFAULT_UNITS.items():
            if isinstance(value,bool):field=QCheckBox(self.Tr("preferences."+key));layout.addRow(field)
            else:
                field=QDoubleSpinBox();field.setRange(.0001,10000);field.setDecimals(4);field.setSingleStep(.1);layout.addRow(self.Tr("preferences."+key),field)
            self.Controls[key]=field
    def _AddStatistics(self):
        layout=self._Page("statistics")
        for key in STATISTIC_FIELDS:
            field=QCheckBox(self.Tr("statistics.label."+key));layout.addRow(field);self.Controls["stat_"+key]=field
    def _AddHistory(self):
        layout=self._Page("history")
        for key,minimum,maximum,value in (("command_limit",1,1000,100),("memory_mb",1,2048,128)):
            field=RangeInput(minimum,maximum,value,decimals=0);layout.addRow(self.Tr(f"preferences.{key}"),field);self.Controls[key]=field
    def _AddScripting(self):
        layout=self._Page("scripting");success=QCheckBox(self.Tr("preferences.show_compile_success"));layout.addRow(success);self.Controls["show_compile_success"]=success
    def _AddRendering(self):
        layout=self._Page("rendering");backend=QComboBox()
        for name in self.Editor.Runtime.SupportedRenderingBackends():
            backend.addItem(self.Tr("preferences.backend."+name),name)
        layout.addRow(self.Tr("preferences.rendering_backend"),backend)
        self.Controls["rendering_backend"]=backend
    def _AddTools(self):
        layout=self._Page("tools")
        for key in ("compiler_path","sdk_path"):
            row=QHBoxLayout();field=QLineEdit();field.setPlaceholderText(self.Tr("preferences.tools_automatic"));button=QPushButton(self.Tr("preferences.tools_browse"));row.addWidget(field,1);row.addWidget(button)
            def choose(checked=False, key=key, field=field):
                path=QFileDialog.getOpenFileName(self,self.Tr("preferences."+key),field.text())[0] if key=="compiler_path" else QFileDialog.getExistingDirectory(self,self.Tr("preferences."+key),field.text())
                if path:field.setText(path)
            button.clicked.connect(choose);layout.addRow(self.Tr("preferences."+key),row);self.Controls[key]=field
    def _AddFileAssociations(self):
        layout=self._Page("file_associations")
        remember=QCheckBox(self.Tr("preferences.associations_remember"));layout.addRow(remember);self.Controls["remember_associations"]=remember
        self.AssociationList=QTreeWidget();self.AssociationList.setHeaderLabels([self.Tr("preferences.association_format"),self.Tr("preferences.association_application")]);self.AssociationList.setRootIsDecorated(False);self.AssociationList.setMinimumHeight(120);layout.addRow(self.AssociationList)
        controls=QWidget();buttons=QHBoxLayout(controls);buttons.setContentsMargins(0,0,0,0)
        self.AssociationChoose=QPushButton(self.Tr("preferences.association_choose"));self.AssociationReset=QPushButton(self.Tr("preferences.association_reset"));self.AssociationResetAll=QPushButton(self.Tr("preferences.association_reset_all"))
        for button in (self.AssociationChoose,self.AssociationReset,self.AssociationResetAll):buttons.addWidget(button)
        layout.addRow(controls);self.AssociationChoose.clicked.connect(self._ChooseAssociation);self.AssociationReset.clicked.connect(self._ResetAssociation);self.AssociationResetAll.clicked.connect(self._ResetAllAssociations)
        self.AssociationList.currentItemChanged.connect(lambda *_:self._AssociationButtons());self._RefreshAssociations()
    def _RefreshAssociations(self):
        selected=self.AssociationList.currentItem();extension=selected.data(0,Qt.ItemDataRole.UserRole) if selected else None
        self.AssociationList.clear()
        for suffix in sorted(self.Editor.AssetBrowser.ExternalOpener.Extensions):
            value=self._associations.get(suffix);app=value.get("application") if isinstance(value,dict) and value.get("platform")==sys.platform else None
            app=app if isinstance(app,str) else None
            text=Path(app).name if isinstance(app,str) and app else self.Tr("preferences.association_unassigned")
            item=QTreeWidgetItem([suffix,text]);item.setData(0,Qt.ItemDataRole.UserRole,suffix);item.setToolTip(1,app or text);self.AssociationList.addTopLevelItem(item)
            if suffix==extension:self.AssociationList.setCurrentItem(item)
        self.AssociationList.resizeColumnToContents(0);self._AssociationButtons()
    def _AssociationButtons(self):
        selected=self.AssociationList.currentItem();self.AssociationChoose.setEnabled(selected is not None);self.AssociationReset.setEnabled(bool(selected and selected.data(0,Qt.ItemDataRole.UserRole) in self._associations));self.AssociationResetAll.setEnabled(bool(self._associations))
    def _ChooseAssociation(self):
        item=self.AssociationList.currentItem()
        if item is None:return
        suffix=item.data(0,Qt.ItemDataRole.UserRole)
        try:application=ChooseApplication(self,self.Editor.Localization,suffix)
        except (OSError,ValueError) as error:QMessageBox.warning(self,self.Tr("preferences.title"),self.Tr("assets.open_failed",error=str(error)));return
        if application:self._associations[suffix]={"platform":sys.platform,"application":str(application)};self._RefreshAssociations()
    def _ResetAssociation(self):
        item=self.AssociationList.currentItem()
        if item:self._associations.pop(item.data(0,Qt.ItemDataRole.UserRole),None);self._RefreshAssociations()
    def _ResetAllAssociations(self):self._associations.clear();self._RefreshAssociations()
    def _RestoreDefaults(self):self.SetValues(DEFAULT_PREFERENCES);self._ResetAllAssociations()
    @staticmethod
    def _ComboSet(combo,value):index=combo.findData(value);combo.setCurrentIndex(max(0,index))
    def SetValues(self,value:dict)->None:
        self.Controls["expand_new_hierarchy_items"].setChecked(bool(MergePreferences(value)["general"]["expand_new_hierarchy_items"]))
        p=MergePreferences(value);c=self.Controls;c["confirm_unsaved"].setChecked(bool(p["general"]["confirm_unsaved"]));c["save_workspace"].setChecked(bool(p["general"]["save_workspace"]));self._ComboSet(c["theme"],p["appearance"]["theme"]);self._ComboSet(c["locale"],p["appearance"]["locale"]);c["navigation_speed"].setValue(float(p["scene"]["navigation_speed"]));c["grid_visible"].setChecked(bool(p["scene"]["grid_visible"]));self._ComboSet(c["grid_plane"],int(p["scene"]["grid_plane"]));c["clear_on_play"].setChecked(bool(p["console"]["clear_on_play"]));c["show_compile_success"].setChecked(bool(p["scripting"]["show_compile_success"]))
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible","high_level_selection"):c[key].setChecked(bool(p["scene"][key]))
        self._ComboSet(c["shading_mode"],p["scene"]["shading_mode"])
        for key in ("look_sensitivity","fly_boost"):c[key].SetValue(p["scene"][key])
        for key in ("command_limit","memory_mb"):c[key].SetValue(p["history"][key])
        self._ComboSet(c["rendering_backend"],p["rendering"]["backend"])
        c["remember_associations"].setChecked(bool(p["file_associations"]["remember"]))
        for key in ("compiler_path","sdk_path"):c[key].setText(str(p["tools"][key]))
        for key,value in p["transform"].items():
            c[key].setChecked(value) if isinstance(value,bool) else c[key].setValue(value)
        for key in STATISTIC_FIELDS:c["stat_"+key].setChecked(bool(p["statistics"][key]))
    def Values(self)->dict:
        c=self.Controls;result=MergePreferences(self.Editor.GetPreferences())
        result.update(general={"confirm_unsaved":c["confirm_unsaved"].isChecked(),"save_workspace":c["save_workspace"].isChecked()},appearance={"theme":c["theme"].currentData(),"locale":c["locale"].currentData()},console={"clear_on_play":c["clear_on_play"].isChecked()},scripting={"show_compile_success":c["show_compile_success"].isChecked()})
        result["scene"].update(navigation_speed=c["navigation_speed"].value(),grid_visible=c["grid_visible"].isChecked(),grid_plane=c["grid_plane"].currentData(),shading_mode=c["shading_mode"].currentData())
        result["general"]["expand_new_hierarchy_items"]=c["expand_new_hierarchy_items"].isChecked()
        for key in ("pivot_center","local_space","gizmos_visible","stats_visible","icons_visible","orientation_visible","high_level_selection"):result["scene"][key]=c[key].isChecked()
        for key in ("look_sensitivity","fly_boost"):result["scene"][key]=c[key].GetValue()
        result["history"]={key:int(c[key].GetValue()) for key in ("command_limit","memory_mb")}
        result["rendering"]={"backend":c["rendering_backend"].currentData() or "automatic"}
        result["file_associations"]={"remember":c["remember_associations"].isChecked()}
        result["tools"]={key:c[key].text().strip() for key in ("compiler_path","sdk_path")}
        result["transform"]={key:c[key].isChecked() if isinstance(value,bool) else c[key].value() for key,value in DEFAULT_UNITS.items()}
        result["statistics"]={key:c["stat_"+key].isChecked() for key in STATISTIC_FIELDS}
        return result
    def _Apply(self)->None:
        opener=self.Editor.AssetBrowser.ExternalOpener;previous=opener.Associations()
        self.Editor._settings[opener.SettingsKey]=deepcopy(self._associations)
        try:self.Editor.ApplyPreferences(self.Values())
        except (OSError,ValueError) as error:
            self.Editor._settings[opener.SettingsKey]=previous
            QMessageBox.warning(self,self.Tr("preferences.title"),self.Tr("assets.open_failed",error=str(error)));return
        self.accept()

__all__=["DEFAULT_PREFERENCES","MergePreferences","PreferencesDialog"]
