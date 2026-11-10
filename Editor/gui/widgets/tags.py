"""Project tag choices with a lazily created, checkable popup."""
import json
from shiboken6 import isValid
from PySide6.QtCore import QEvent, QPoint, Qt, Signal, QSignalBlocker
from PySide6.QtWidgets import QToolButton, QLineEdit, QMenu, QWidgetAction, QStyle, QStyleOptionToolButton, QStylePainter
from .fields import FieldStateSupport
from ...tags import IsValidTagName


class TagInput(FieldStateSupport, QToolButton):
    SelectionChanged = Signal(tuple)

    def __init__(self, controller, values, changed):
        super().__init__()
        self.Controller = controller
        self._catalogue = set()
        self._values = ()
        self._menu = None
        self._clear_action = None
        project = controller.Runtime.ProjectInfo()
        try:
            stored = json.loads(project.get('properties', {}).get('Editor.Tags', '[]'))
            if isinstance(stored, list):
                self._catalogue.update(name for name in stored if self._Valid(name))
        except (ValueError, TypeError):
            pass
        self._catalogue.update(getattr(controller, '_tag_catalogue', set()))
        self.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
        self.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        self.clicked.connect(self.showPopup)
        self.SetValues(values)
        self.SelectionChanged.connect(lambda selected: changed(list(selected)))

    _Valid = staticmethod(IsValidTagName)

    def GetValues(self):
        return self._values

    def GetValue(self):
        return ','.join(self._values)

    def SetValue(self, value):
        self.SetValues(value.split(',') if isinstance(value, str) and value else value or [])

    def SetValues(self, values):
        selected = tuple(sorted(set(name for name in values if self._Valid(name))))
        if len(selected) > 64:
            raise ValueError('An entity supports at most 64 tags')
        changed = selected != self._values
        self._values = selected
        self._catalogue.update(selected)
        label = ', '.join(selected) or self.Controller.Window.Localization.Translate('tags.none')
        self.setText(label)
        self.setToolTip(label)
        if self._menu is not None:
            for action in self._menu.actions():
                if not action.isCheckable():
                    continue
                blocker = QSignalBlocker(action)
                action.setChecked(not selected if action is self._clear_action else action.data() in selected)
                action.setEnabled(action is self._clear_action or action.isChecked() or len(selected) < 64)
                del blocker
        if changed:
            self.SelectionChanged.emit(selected)

    def _Toggle(self, name, checked):
        selected = set(self._values)
        if checked:
            selected.add(name)
        else:
            selected.discard(name)
        self.SetValues(selected)

    def sizeHint(self):
        size = super().sizeHint()
        size.setWidth(size.width() + 16)
        return size

    def paintEvent(self, event):
        option = QStyleOptionToolButton()
        self.initStyleOption(option)
        option.features |= QStyleOptionToolButton.ToolButtonFeature.HasMenu
        painter = QStylePainter(self)
        painter.drawComplexControl(QStyle.ComplexControl.CC_ToolButton, option)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Down:
            self.showPopup()
            event.accept()
            return
        super().keyPressEvent(event)

    def showPopup(self):
        # Entity selection must not create popup windows or native handles.
        if self._menu is not None:
            return
        tr = self.Controller.Window.Localization.Translate
        self._catalogue.update(self.Controller.Runtime.TagNames())
        menu = self._menu = QMenu(self)
        menu.installEventFilter(self)
        clear = self._clear_action = menu.addAction(tr('tags.none'))
        clear.setCheckable(True)
        clear.setChecked(not self._values)
        clear.triggered.connect(lambda: self.SetValues(()))
        menu.addSeparator()
        for name in sorted(name for name in self._catalogue if self._Valid(name)):
            action = menu.addAction(name)
            action.setData(name)
            action.setCheckable(True)
            action.setChecked(name in self._values)
            action.setEnabled(action.isChecked() or len(self._values) < 64)
            action.toggled.connect(lambda checked, name=name: self._Toggle(name, checked))
        menu.addSeparator()
        editor = QLineEdit(menu)
        editor.setPlaceholderText(tr('tags.add'))
        action = QWidgetAction(menu)
        action.setDefaultWidget(editor)
        menu.addAction(action)
        editor.returnPressed.connect(lambda: self._Add(editor.text()))
        try:
            menu.exec(self.mapToGlobal(QPoint(0, self.height())))
        finally:
            self._menu = None
            self._clear_action = None
            if isValid(menu):menu.deleteLater()
            if isValid(self):
                self.setDown(False)
                self.update()

    def eventFilter(self, watched, event):
        action = None
        if watched is self._menu:
            if event.type() == QEvent.Type.MouseButtonRelease and event.button() == Qt.MouseButton.LeftButton:
                action = self._menu.actionAt(event.position().toPoint())
            elif event.type() == QEvent.Type.KeyPress and event.key() == Qt.Key.Key_Space:
                action = self._menu.activeAction()
        if action is not None and action.isCheckable() and action.isEnabled():
            action.trigger()
            return True
        return super().eventFilter(watched, event)

    def _Add(self, name):
        name = name.strip(' ')
        if not self._Valid(name) or name not in self._values and len(self._values) >= 64:
            return
        self._catalogue.add(name)
        self.Controller._tag_catalogue = set(self._catalogue)
        project = self.Controller.Runtime.ProjectInfo()
        if project.get('project_path'):
            properties = dict(project.get('properties', {}))
            properties['Editor.Tags'] = json.dumps(sorted(self._catalogue), ensure_ascii=False)
            self.Controller.Runtime.UpdateProjectInfo(project['name'], properties)
        self.SetValues((*self._values, name))
        if self._menu is not None:
            self._menu.close()
