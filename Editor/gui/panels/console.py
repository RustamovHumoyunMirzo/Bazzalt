"""Filterable, item-based editor console."""
from __future__ import annotations
from dataclasses import dataclass
from enum import Enum
from time import monotonic
from PySide6.QtCore import QSize,Qt,Signal
from PySide6.QtGui import QAction,QBrush,QColor,QIcon,QKeySequence
from PySide6.QtWidgets import (QAbstractItemView,QApplication,QComboBox,QHBoxLayout,QLineEdit,QListWidget,QListWidgetItem,QMenu,QPushButton,QVBoxLayout,QWidget)
from ...localization import LocalizationManager
from ..widgets.fields import MultiSelectInput

class ConsoleLevel(Enum):
    Info="info";Warning="warning";Error="error"

@dataclass(slots=True)
class ConsoleMessage:
    Text:str
    Level:ConsoleLevel=ConsoleLevel.Info
    ShowIcon:bool=True
    Source:str="Editor"
    Id:int=0
    Timestamp:float=0

class ConsoleList(QListWidget):
    def toPlainText(self)->str:return "\n".join(self.item(index).text() for index in range(self.count()))

class ConsolePanel(QWidget):
    ContextMenuRequested=Signal(object,object)
    def __init__(self,localization:LocalizationManager,resources=None,themes=None,install_shortcuts=True)->None:
        super().__init__();self._localization=localization;self._resources=resources;self._themes=themes;self._messages:list[ConsoleMessage]=[];self._visible_levels=set(ConsoleLevel)
        layout=QVBoxLayout(self);layout.setContentsMargins(4,4,4,4);layout.setSpacing(4)
        filters=QHBoxLayout();filters.setSpacing(4);self.Search=QLineEdit();self.Search.setObjectName("ConsoleSearch");self.Search.setClearButtonEnabled(True);self.Search.textChanged.connect(self._Refresh);self.SourceFilter=QComboBox();self.SourceFilter.setObjectName("ConsoleSourceFilter");self.SourceFilter.currentIndexChanged.connect(self._Refresh);filters.addWidget(self.Search,1);filters.addWidget(self.SourceFilter)
        self.LevelFilter=MultiSelectInput();self.LevelFilter.setObjectName("ConsoleLevelFilter");self.LevelFilter.SelectionChanged.connect(self._LevelsChanged);filters.addWidget(self.LevelFilter)
        actions=QHBoxLayout();actions.setSpacing(4);self.ClearButton=QPushButton();self.ClearFilteredButton=QPushButton();self.RemoveButton=QPushButton();self.ClearButton.clicked.connect(self.Clear);self.ClearFilteredButton.clicked.connect(self.ClearFiltered);self.RemoveButton.clicked.connect(self.RemoveSelected);actions.addWidget(self.ClearButton);actions.addWidget(self.ClearFilteredButton);actions.addWidget(self.RemoveButton);actions.addStretch()
        self.View=ConsoleList();self.View.setObjectName("ConsoleMessageList");self.View.setIconSize(QSize(16,16));self.View.setUniformItemSizes(True);self.View.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection);self.View.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff);self.View.setWordWrap(True);self.View.setTextElideMode(Qt.TextElideMode.ElideRight);self.View.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);self.View.customContextMenuRequested.connect(self._ShowContextMenu);self.View.itemSelectionChanged.connect(lambda:self.RemoveButton.setEnabled(bool(self.View.selectedItems())))
        if install_shortcuts:
            copyAction=QAction(self);copyAction.setShortcut(QKeySequence.StandardKey.Copy);copyAction.setShortcutContext(Qt.ShortcutContext.WidgetWithChildrenShortcut);copyAction.triggered.connect(self._CopySelected);self.View.addAction(copyAction)
            deleteAction=QAction(self);deleteAction.setShortcut(QKeySequence.StandardKey.Delete);deleteAction.setShortcutContext(Qt.ShortcutContext.WidgetWithChildrenShortcut);deleteAction.triggered.connect(self.RemoveSelected);self.View.addAction(deleteAction)
        layout.addLayout(filters);layout.addLayout(actions);layout.addWidget(self.View,1);localization.LocaleChanged.connect(lambda _:self._Retranslate())
        if themes is not None:themes.ThemeChanged.connect(lambda _theme:self._Refresh())
        self._SyncSources();self._Retranslate();self.RemoveButton.setEnabled(False)
    def AddMessage(self,text:str,level:ConsoleLevel=ConsoleLevel.Info,show_icon:bool=True,source:str="Editor")->None:
        if not isinstance(level,ConsoleLevel):raise TypeError("level must be a ConsoleLevel")
        if getattr(self,"_runtime",None) is not None:
            self._runtime.ConsoleAdd(str(text),{ConsoleLevel.Info:1,ConsoleLevel.Warning:2,ConsoleLevel.Error:4}[level],str(source).strip() or "Editor",show_icon);self.RefreshNative();return
        source=str(source).strip() or "Editor";message=ConsoleMessage(str(text),level,bool(show_icon),source);self._messages.append(message);self._SyncSources()
        if self._Matches(message):self._AddItem(message,len(self._messages)-1);self.View.scrollToBottom()
    def BindRuntime(self,runtime)->None:
        if getattr(self,"_runtime",None) is runtime:return
        snapshot=runtime.ConsoleSnapshot(0)
        if not isinstance(snapshot,dict):return
        self._runtime=runtime;self._console_revision=0
        for m in self._messages:runtime.ConsoleAdd(m.Text,{ConsoleLevel.Info:1,ConsoleLevel.Warning:2,ConsoleLevel.Error:4}[m.Level],m.Source,m.ShowIcon)
        self.RefreshNative()
    def RefreshNative(self,force:bool=True)->None:
        runtime=getattr(self,"_runtime",None)
        if runtime is None:return
        now=monotonic()
        if not force and now-getattr(self,"_console_last_poll",0)<.1:return
        self._console_last_poll=now
        snapshot=runtime.ConsoleSnapshot(self._console_revision)
        if not isinstance(snapshot,dict):return
        levels={1:ConsoleLevel.Info,2:ConsoleLevel.Warning,4:ConsoleLevel.Error}
        messages=[ConsoleMessage(m["text"],levels[m["level"]],m["icon"],m["source"],m["id"],m["timestamp"]) for m in snapshot["messages"]]
        count=len(self._messages);append=len(messages)>=count and all(a.Id==b.Id for a,b in zip(self._messages,messages))
        self._console_revision=snapshot["revision"];self._messages=messages;self._SyncSources()
        if append:
            for index in range(count,len(messages)):
                if self._Matches(messages[index]):self._AddItem(messages[index],index)
            if len(messages)>count:self.View.scrollToBottom()
        else:self._Refresh()
    def Clear(self)->None:
        if getattr(self,"_runtime",None) is not None:self._runtime.ConsoleClear();self.RefreshNative();return
        self._messages.clear();self.View.clear();self._SyncSources()
    def ClearFiltered(self)->None:
        if getattr(self,"_runtime",None) is not None:
            self.RefreshNative()
            self._runtime.ConsoleRemove([m.Id for m in self._messages if self._Matches(m)]);self.RefreshNative();return
        self._messages=[message for message in self._messages if not self._Matches(message)];self._SyncSources();self._Refresh()
    def RemoveSelected(self)->None:
        indices={int(item.data(Qt.ItemDataRole.UserRole)) for item in self.View.selectedItems()}
        if not indices:return
        if getattr(self,"_runtime",None) is not None:
            self._runtime.ConsoleRemove([m.Id for index,m in enumerate(self._messages) if index in indices]);self.RefreshNative();return
        self._messages=[message for index,message in enumerate(self._messages) if index not in indices];self._SyncSources();self._Refresh()
    def GetMessages(self)->tuple[ConsoleMessage,...]:self.RefreshNative();return tuple(self._messages)
    def SetLevelVisible(self,level:ConsoleLevel,visible:bool)->None:
        if visible:self._visible_levels.add(level)
        else:self._visible_levels.discard(level)
        values=tuple(item.value for item in ConsoleLevel if item in self._visible_levels)
        if set(self.LevelFilter.GetValues())!=set(values):self.LevelFilter.SetValues(values)
        self._Refresh()
    def _LevelsChanged(self,values:tuple)->None:
        self._visible_levels={level for level in ConsoleLevel if level.value in values};self.LevelFilter.setText(self._localization.Translate("console.filters"));self._Refresh()
    def SetSourceFilter(self,source:str="")->None:
        index=self.SourceFilter.findData(source)
        self.SourceFilter.setCurrentIndex(max(0,index))
    def _SyncSources(self)->None:
        current=self.SourceFilter.currentData() if self.SourceFilter.count() else "";self.SourceFilter.blockSignals(True);self.SourceFilter.clear();self.SourceFilter.addItem(self._localization.Translate("console.all_sources"),"")
        sources={message.Source for message in self._messages}
        if current:sources.add(str(current))
        for source in sorted(sources,key=str.casefold):self.SourceFilter.addItem(source,source)
        index=self.SourceFilter.findData(current);self.SourceFilter.setCurrentIndex(max(0,index));self.SourceFilter.blockSignals(False)
    def _Matches(self,message:ConsoleMessage)->bool:
        source=self.SourceFilter.currentData() or "";query=self.Search.text().strip().casefold()
        return message.Level in self._visible_levels and (not source or message.Source==source) and (not query or query in message.Text.casefold() or query in message.Source.casefold())
    def _ThemeName(self)->str:
        if self._themes is None:return "dark"
        return "light" if self._themes.GetTheme().background.lower()=="#d4d4d4" else "dark"
    def _Icon(self,message:ConsoleMessage)->QIcon:
        if not message.ShowIcon or self._resources is None:return QIcon()
        filename={ConsoleLevel.Info:f"info_{self._ThemeName()}.svg",ConsoleLevel.Warning:"warn.svg",ConsoleLevel.Error:"error.svg"}[message.Level]
        return self._resources.Icon(f"icons/console/{filename}")
    def _AddItem(self,message:ConsoleMessage,index:int)->None:
        text=f"{message.Text}  —  {message.Source}";item=QListWidgetItem(self._Icon(message),text);item.setData(Qt.ItemDataRole.UserRole,index);item.setData(Qt.ItemDataRole.UserRole+1,message.Id or id(message));item.setToolTip(f"{message.Text}\nSource: {message.Source}");item.setFlags(Qt.ItemFlag.ItemIsEnabled|Qt.ItemFlag.ItemIsSelectable)
        if message.Level is ConsoleLevel.Warning:item.setForeground(QBrush(QColor("#e8b85c")))
        elif message.Level is ConsoleLevel.Error:item.setForeground(QBrush(QColor("#ee6a6a")))
        self.View.addItem(item)
    def _Refresh(self,*_args)->None:
        selected={item.data(Qt.ItemDataRole.UserRole+1) for item in self.View.selectedItems()};self.View.clear()
        for index,message in enumerate(self._messages):
            if self._Matches(message):self._AddItem(message,index);self.View.item(self.View.count()-1).setSelected((message.Id or id(message)) in selected)
    def _CopySelected(self)->None:
        items=self.View.selectedItems()
        if items:QApplication.clipboard().setText("\n".join(item.text() for item in items))
    def _ShowContextMenu(self,position)->None:
        item=self.View.itemAt(position)
        if item is not None:
            if not item.isSelected():self.View.clearSelection();item.setSelected(True)
            self.View.setCurrentItem(item)
        menu=QMenu(self);copy=menu.addAction(self._localization.Translate("console.copy"));copy.setShortcut(QKeySequence.StandardKey.Copy);copy.setEnabled(bool(self.View.selectedItems()));copy.triggered.connect(self._CopySelected);remove=menu.addAction(self._localization.Translate("console.remove_selected"));remove.setEnabled(bool(self.View.selectedItems()));remove.triggered.connect(self.RemoveSelected);menu.addSeparator();filtered=menu.addAction(self._localization.Translate("console.clear_filtered"));filtered.setEnabled(self.View.count()>0);filtered.triggered.connect(self.ClearFiltered);clear=menu.addAction(self._localization.Translate("console.clear"));clear.setEnabled(bool(self._messages));clear.triggered.connect(self.Clear)
        global_position=self.View.viewport().mapToGlobal(position);self.ContextMenuRequested.emit(menu,global_position);menu.exec(global_position)
    def _Retranslate(self)->None:
        self.Search.setPlaceholderText(self._localization.Translate("console.search"));self.ClearButton.setText(self._localization.Translate("console.clear"));self.ClearFilteredButton.setText(self._localization.Translate("console.clear_filtered"));self.RemoveButton.setText(self._localization.Translate("console.remove_selected"))
        labels={ConsoleLevel.Info:"console.info",ConsoleLevel.Warning:"console.warning",ConsoleLevel.Error:"console.error"}
        selected=tuple(level.value for level in self._visible_levels);self.LevelFilter.SetOptions([(self._localization.Translate(labels[level]),level.value) for level in ConsoleLevel]);self.LevelFilter.SetValues(selected);self.LevelFilter.setText(self._localization.Translate("console.filters"))
        self._SyncSources()

__all__=["ConsoleLevel","ConsoleMessage","ConsolePanel"]
