"""Inspector-style component and property editor."""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QFormLayout, QFrame, QMenu, QPushButton, QScrollArea, QToolButton, QVBoxLayout,
    QWidget,
)

from ...localization import LocalizationManager


class ComponentSection(QFrame):
    def __init__(self, title: str, expanded: bool = True) -> None:
        super().__init__(); self.setObjectName("ComponentSection")
        layout=QVBoxLayout(self);layout.setContentsMargins(4,4,4,4);layout.setSpacing(3)
        self.Toggle=QToolButton();self.Toggle.setText(title);self.Toggle.setCheckable(True);self.Toggle.setChecked(expanded)
        self.Toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.Body=QWidget();self.Form=QFormLayout(self.Body);self.Form.setContentsMargins(16,2,4,5)
        layout.addWidget(self.Toggle);layout.addWidget(self.Body)
        self.Toggle.toggled.connect(self.SetExpanded);self.SetExpanded(expanded)

    def SetExpanded(self, expanded: bool) -> None:
        self.Toggle.setArrowType(Qt.ArrowType.DownArrow if expanded else Qt.ArrowType.RightArrow)
        self.Body.setVisible(expanded)

    def AddField(self, label: str, editor: QWidget) -> None: self.Form.addRow(label, editor)


class PropertiesPanel(QWidget):
    AddComponentRequested = Signal()
    ContextMenuRequested = Signal(object, object)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__();self._localization=localization;self._sections:dict[str,ComponentSection]={}
        layout=QVBoxLayout(self);layout.setContentsMargins(4,4,4,4);layout.setSpacing(4)
        self.Scroll=QScrollArea();self.Scroll.setWidgetResizable(True);self.Scroll.setFrameShape(QFrame.Shape.NoFrame)
        self.Container=QWidget();self.ComponentsLayout=QVBoxLayout(self.Container);self.ComponentsLayout.setContentsMargins(0,0,0,0);self.ComponentsLayout.addStretch()
        self.Container.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.Container.customContextMenuRequested.connect(self._ShowContextMenu)
        self.Scroll.setWidget(self.Container);layout.addWidget(self.Scroll,1)
        self.AddComponentButton=QPushButton();self.AddComponentButton.setObjectName("AddComponentButton")
        self.AddComponentButton.clicked.connect(self.AddComponentRequested);layout.addWidget(self.AddComponentButton)
        localization.LocaleChanged.connect(lambda _: self._Retranslate());self._Retranslate()

    def AddComponentSection(self, component_id: str, title: str,
                            expanded: bool = True) -> ComponentSection:
        if component_id in self._sections:return self._sections[component_id]
        section=ComponentSection(title,expanded);self._sections[component_id]=section
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
