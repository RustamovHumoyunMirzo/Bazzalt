"""Inspector-style component and property editor."""

from __future__ import annotations

import json

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction, QIcon, QPaintEvent
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QFrame, QGridLayout, QHBoxLayout, QLabel, QMenu, QPushButton, QScrollArea, QSizePolicy, QStyle, QStyleOptionButton, QStylePainter, QToolButton, QVBoxLayout,
    QWidget,
)

from ...localization import LocalizationManager
from ..widgets.options_button import OptionsButton
from ..display_names import DisplayName


class CenteredCheckBox(QCheckBox):
    """Paint a stylesheet-aware checkbox indicator exactly at widget center."""
    def paintEvent(self,event:QPaintEvent)->None:
        option=QStyleOptionButton();self.initStyleOption(option)
        indicator=self.style().subElementRect(QStyle.SubElement.SE_CheckBoxIndicator,option,self)
        option.rect=QStyle.alignedRect(self.layoutDirection(),Qt.AlignmentFlag.AlignCenter,indicator.size(),self.rect())
        painter=QStylePainter(self);painter.drawControl(QStyle.ControlElement.CE_CheckBox,option)


class ComponentSection(QFrame):
    RemoveRequested = Signal()
    EnabledChanged = Signal(bool)
    def __init__(self, title: str, expanded: bool = True, removable: bool = True,
                 localization: LocalizationManager | None = None,
                 icon: QIcon | None = None, enabled:bool|None=None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent); self.setObjectName("ComponentSection")
        self.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Fixed)
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0);layout.setSpacing(0)
        header=QWidget(self);header.setObjectName("ComponentHeaderRow");headerLayout=QHBoxLayout(header);headerLayout.setContentsMargins(3,0,2,0);headerLayout.setSpacing(3)
        self.Toggle=QToolButton(header);self.Toggle.setText(title);self.Toggle.setCheckable(True);self.Toggle.setChecked(expanded)
        self.Toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.Toggle.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Fixed)
        self.Toggle.setObjectName("ComponentHeader")
        self._fields:dict[str,QWidget]={};self._localization=localization;self._removable=removable;self._title=title;self._captions={}
        if localization:localization.LocaleChanged.connect(self._RetranslateLabels)
        self.Body=QWidget(self);self.Form=QGridLayout(self.Body);self.Form.setContentsMargins(8,6,8,6)
        self.Form.setHorizontalSpacing(14);self.Form.setVerticalSpacing(6);self.Form.setColumnStretch(1,1)
        self.Form.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.IconLabel=QLabel(header);self.IconLabel.setObjectName("ComponentIcon");self.IconLabel.setFixedSize(16,16);self.IconLabel.setVisible(icon is not None and not icon.isNull())
        if icon is not None and not icon.isNull():self.IconLabel.setPixmap(icon.pixmap(16,16))
        headerLayout.addWidget(self.IconLabel)
        enabledSlot=QWidget(header);enabledSlot.setObjectName("ComponentEnabledSlot");enabledSlot.setFixedSize(20,21)
        enabledLayout=QHBoxLayout(enabledSlot);enabledLayout.setContentsMargins(0,0,0,0);enabledLayout.setSpacing(0)
        self.Enabled=CenteredCheckBox(enabledSlot);self.Enabled.setObjectName("ComponentEnabled");self.Enabled.setFixedSize(20,21);self.Enabled.setToolTip("Enable component");enabledSlot.setVisible(enabled is not None)
        if enabled is not None:self.Enabled.setChecked(enabled)
        self.Enabled.toggled.connect(self.EnabledChanged)
        enabledLayout.addWidget(self.Enabled,0,Qt.AlignmentFlag.AlignCenter)
        headerLayout.addWidget(enabledSlot);headerLayout.addWidget(self.Toggle,1)
        options=OptionsButton(header);options.setObjectName("ComponentOptionsButton");options.setToolTip(localization.Translate("hierarchy.helpers") if localization else "Options");options.clicked.connect(lambda:self._ShowOptions(options));headerLayout.addWidget(options)
        layout.addWidget(header);layout.addWidget(self.Body)
        self.Toggle.toggled.connect(self.SetExpanded);self.SetExpanded(expanded)

    def SetExpanded(self, expanded: bool) -> None:
        self.Toggle.setArrowType(Qt.ArrowType.DownArrow if expanded else Qt.ArrowType.RightArrow)
        self.Body.setVisible(expanded)

    def AddField(self, label: str, editor: QWidget) -> None:
        row=self.Form.rowCount();caption=QLabel(DisplayName(self._localization,label,"field"),self.Body);caption.setObjectName("InspectorFieldLabel");self._captions[label]=caption
        caption.setWordWrap(True);caption.setToolTip(caption.text())
        caption.setMinimumWidth(72);caption.setMaximumWidth(120);caption.setSizePolicy(QSizePolicy.Policy.Preferred,QSizePolicy.Policy.Fixed);caption.setAlignment(Qt.AlignmentFlag.AlignLeading|Qt.AlignmentFlag.AlignVCenter)
        editor.setParent(self.Body)
        editor.setMinimumWidth(0);editor.setMaximumWidth(260);editor.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Fixed)
        self.Form.setRowMinimumHeight(row,24)
        self.Form.addWidget(caption,row,0,Qt.AlignmentFlag.AlignVCenter);self.Form.addWidget(editor,row,1,Qt.AlignmentFlag.AlignVCenter)
        self._fields[label]=editor

    def _RetranslateLabels(self,*_):
        self.Toggle.setText(DisplayName(self._localization,self._title))
        self.Enabled.setToolTip(self._Text("properties.enable_component","Enable component"))
        for key,caption in self._captions.items():caption.setText(DisplayName(self._localization,key,"field"));caption.setToolTip(caption.text())

    def _Text(self,key:str,fallback:str)->str:
        return self._localization.Translate(key) if self._localization else fallback

    def AddViewport(self,viewport:QWidget)->None:
        viewport.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Fixed)
        self.Form.addWidget(viewport,self.Form.rowCount(),0,1,2,Qt.AlignmentFlag.AlignTop)

    def _ShowOptions(self,button:QToolButton)->None:
        menu=QMenu(self)
        toggle=menu.addAction(self._Text("properties.collapse","Collapse") if self.Toggle.isChecked() else self._Text("properties.expand","Expand"))
        toggle.triggered.connect(lambda:self.Toggle.setChecked(not self.Toggle.isChecked()))
        menu.addSeparator();copy=menu.addAction(self._Text("properties.copy_fields","Copy Fields"));paste=menu.addAction(self._Text("properties.paste_fields","Paste Fields"))
        copy.triggered.connect(self._CopyFields);paste.triggered.connect(self._PasteFields)
        menu.addSeparator();remove=menu.addAction(self._Text("properties.remove_component","Remove Component"));remove.setEnabled(self._removable);remove.triggered.connect(lambda:self.RemoveRequested.emit())
        menu.exec(button.mapToGlobal(button.rect().bottomRight()))

    def _CopyFields(self)->None:
        values={}
        for name,editor in self._fields.items():
            getter=getattr(editor,"GetValue",None)
            if callable(getter):values[name]=getter()
        QApplication.clipboard().setText(json.dumps(values,separators=(",",":")))

    def _PasteFields(self)->None:
        try:values=json.loads(QApplication.clipboard().text())
        except (TypeError,ValueError):return
        if not isinstance(values,dict):return
        for name,value in values.items():
            editor=self._fields.get(name);setter=getattr(editor,"SetValue",None) if editor else None
            if callable(setter):
                try:setter(value)
                except (TypeError,ValueError):pass


class PropertiesPanel(QWidget):
    AddComponentRequested = Signal()
    RemoveComponentRequested = Signal(str)
    ContextMenuRequested = Signal(object, object)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__();self.setObjectName("PropertiesPanel");self._localization=localization;self._sections:dict[str,ComponentSection]={}
        layout=QVBoxLayout(self);layout.setContentsMargins(3,3,3,3);layout.setSpacing(3)
        self.Scroll=QScrollArea(self);self.Scroll.setWidgetResizable(True);self.Scroll.setFrameShape(QFrame.Shape.NoFrame);self.Scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.Container=QWidget(self.Scroll);self.ComponentsLayout=QVBoxLayout(self.Container);self.ComponentsLayout.setContentsMargins(0,0,0,0);self.ComponentsLayout.setSpacing(6);self.ComponentsLayout.addStretch()
        self.Container.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.Container.customContextMenuRequested.connect(self._ShowContextMenu)
        self.Scroll.setWidget(self.Container);layout.addWidget(self.Scroll,1)
        self.AddComponentButton=QPushButton(self);self.AddComponentButton.setObjectName("AddComponentButton")
        self.AddComponentButton.clicked.connect(self.AddComponentRequested);layout.addWidget(self.AddComponentButton)
        localization.LocaleChanged.connect(lambda _: self._Retranslate());self._Retranslate()

    def AddComponentSection(self, component_id: str, title: str,
                            expanded: bool = True, removable: bool = True,
                            icon: QIcon | None = None,
                            enabled:bool|None=None) -> ComponentSection:
        if component_id in self._sections:return self._sections[component_id]
        section=ComponentSection(title,expanded,removable,self._localization,icon,enabled,self.Container);section._RetranslateLabels();section.RemoveRequested.connect(lambda:self.RemoveComponentRequested.emit(component_id));self._sections[component_id]=section
        self.ComponentsLayout.insertWidget(self.ComponentsLayout.count()-1,section);return section

    def RemoveComponentSection(self, component_id: str) -> bool:
        section=self._sections.pop(component_id,None)
        if section is None:return False
        section.deleteLater();return True

    def Clear(self) -> None:
        for section in self._sections.values():section.deleteLater()
        self._sections.clear()

    def SetAllExpanded(self, expanded: bool) -> None:
        for section in self._sections.values():
            section.Toggle.setChecked(expanded)

    def _ShowContextMenu(self, position) -> None:  # type: ignore[no-untyped-def]
        menu = QMenu(self)
        expand = QAction(self._localization.Translate("properties.expand_all"), menu)
        collapse = QAction(self._localization.Translate("properties.collapse_all"), menu)
        expand.triggered.connect(lambda: self.SetAllExpanded(True))
        collapse.triggered.connect(lambda: self.SetAllExpanded(False))
        menu.addActions((expand, collapse))
        global_position = self.Container.mapToGlobal(position)
        self.ContextMenuRequested.emit(menu, global_position)
        menu.exec(global_position)

    def _Retranslate(self) -> None:
        self.AddComponentButton.setText(self._localization.Translate("properties.add_component"))


__all__ = ["ComponentSection", "PropertiesPanel"]
