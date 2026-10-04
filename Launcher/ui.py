"""Independent, platform-native Qt Widgets interface for BAZZALT Hub."""
from __future__ import annotations

from pathlib import Path
from PySide6.QtCore import QByteArray, QDateTime, QEvent, QLocale, QSize, QSortFilterProxyModel, Qt, QUrl
from PySide6.QtGui import QAction, QDesktopServices, QKeySequence, QPalette, QStandardItem, QStandardItemModel
from PySide6.QtWidgets import (
    QApplication, QAbstractItemView, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
    QFileDialog, QFormLayout, QFrame, QGroupBox, QHBoxLayout, QHeaderView,
    QLabel, QLineEdit, QListWidget, QListWidgetItem, QMainWindow, QMenu,
    QMessageBox, QPushButton, QStackedWidget, QStyle, QTableView, QVBoxLayout, QWidget,
)
from bazzalt.branding import LogoIcon
from bazzalt.resources import Package
from bazzalt.settings import DataPaths, Version
from .catalog import CURRENT_EDITOR_VERSION

ID_ROLE = Qt.ItemDataRole.UserRole
SORT_ROLE = int(ID_ROLE) + 1


def Heading(text: str, parent=None) -> QLabel:
    label=QLabel(text,parent);font=label.font();font.setPointSize(font.pointSize()+5);font.setBold(True);label.setFont(font)
    return label


def Muted(text: str, parent=None) -> QLabel:
    label=QLabel(text,parent);label.setForegroundRole(QPalette.ColorRole.PlaceholderText);label.setWordWrap(True)
    return label


def Table(headers: list[str], parent=None) -> tuple[QTableView,QStandardItemModel]:
    table=QTableView(parent);model=QStandardItemModel(0,len(headers),table);model.setHorizontalHeaderLabels(headers);table.setModel(model)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows);table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers);table.setShowGrid(False);table.setAlternatingRowColors(True)
    table.setWordWrap(False);table.setTextElideMode(Qt.TextElideMode.ElideMiddle)
    table.verticalHeader().hide();table.verticalHeader().setDefaultSectionSize(48)
    table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    table.horizontalHeader().setStretchLastSection(True);table.setSortingEnabled(True)
    return table,model


class NewProjectDialog(QDialog):
    def __init__(self, bridge, parent=None):
        super().__init__(parent);self.Bridge=bridge;self.setWindowTitle("New Project");self.setMinimumWidth(480)
        layout=QVBoxLayout(self);layout.setContentsMargins(24,20,24,20);layout.setSpacing(16)
        layout.addWidget(Heading("Create a project",self));layout.addWidget(Muted("Choose a name, location, and installed editor version.",self))
        form=QFormLayout();form.setSpacing(12);self.Name=QLineEdit(self);self.Name.setPlaceholderText("My Game")
        self.Location=QLineEdit(str(bridge.ProjectsRoot()),self);browse=QPushButton("Browse…",self)
        row=QHBoxLayout();row.addWidget(self.Location,1);row.addWidget(browse)
        self.Version=QComboBox(self)
        for editor in sorted(bridge.Catalog.Data["editors"],key=lambda v:Version.Parse(v["version"]),reverse=True):self.Version.addItem(editor["version"],editor["version"])
        form.addRow("Project name",self.Name);form.addRow("Parent folder",row);form.addRow("Editor version",self.Version);layout.addLayout(form)
        self.Preview=Muted("",self);layout.addWidget(self.Preview)
        self.Buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel,self)
        self.CreateButton=self.Buttons.button(QDialogButtonBox.StandardButton.Ok);self.CreateButton.setText("Create Project")
        self.Buttons.accepted.connect(self.Create);self.Buttons.rejected.connect(self.reject);layout.addWidget(self.Buttons)
        browse.clicked.connect(self.Browse);self.Name.textChanged.connect(self.Validate);self.Location.textChanged.connect(self.Validate);self.Validate()

    def Browse(self):
        path=QFileDialog.getExistingDirectory(self,"Project Location",self.Location.text())
        if path:self.Location.setText(path)

    def Validate(self):
        name=self.Name.text().strip();valid=bool(name and name not in {".",".."} and not any(c in name for c in '/\\:\0"\r\n'))
        self.CreateButton.setEnabled(valid and bool(self.Location.text().strip()) and self.Version.count()>0)
        self.Preview.setText(str(Path(self.Location.text()).expanduser()/name) if name else "Your project will be created in its own folder.")

    def Create(self):
        destination=Path(self.Location.text()).expanduser()/self.Name.text().strip()
        if self.Bridge.CreateProject(self.Name.text().strip(),str(destination),self.Version.currentData()):self.accept()


class NativeHubWindow(QMainWindow):
    def __init__(self, catalog, bridge):
        super().__init__();self.Catalog=catalog;self.Bridge=bridge;bridge.setParent(self)
        self.Resources=Package("hub")
        self.setWindowTitle("BAZZALT Hub");self.resize(1100,700);self.setMinimumSize(760,480)
        self.setWindowIcon(LogoIcon(self.palette().color(QPalette.ColorRole.WindowText).name()))
        root=QWidget(self);layout=QHBoxLayout(root);layout.setContentsMargins(0,0,0,0);layout.setSpacing(0);self.setCentralWidget(root)
        sidebar=QWidget(root);sidebar.setMinimumWidth(170);sidebar.setMaximumWidth(210);side=QVBoxLayout(sidebar);side.setContentsMargins(18,24,18,20);side.setSpacing(12)
        brand=QHBoxLayout();self.BrandIcon=QLabel(sidebar);self.BrandIcon.setPixmap(self.windowIcon().pixmap(30,30));brand.addWidget(self.BrandIcon);brand.addWidget(Heading("BAZZALT",sidebar));side.addLayout(brand);side.addWidget(Muted("Engine Hub",sidebar))
        self.Navigation=QListWidget(sidebar);self.Navigation.setFrameShape(QFrame.Shape.NoFrame);self.Navigation.setSpacing(6)
        self.Navigation.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        for text,standard in (("Projects",QStyle.StandardPixmap.SP_DirIcon),("Editors",QStyle.StandardPixmap.SP_ComputerIcon),("Preferences",QStyle.StandardPixmap.SP_FileDialogDetailedView)):
            item=QListWidgetItem(self.style().standardIcon(standard),text);item.setSizeHint(QSize(140,40));self.Navigation.addItem(item)
        side.addWidget(self.Navigation,1);side.addWidget(Muted(f"Hub {CURRENT_EDITOR_VERSION}\nNative desktop application",sidebar))
        separator=QFrame(root);separator.setFrameShape(QFrame.Shape.VLine);layout.addWidget(sidebar);layout.addWidget(separator)
        self.Pages=QStackedWidget(root);layout.addWidget(self.Pages,1)
        self.BuildProjects();self.BuildEditors();self.BuildPreferences();self.BuildActions()
        self.Navigation.currentRowChanged.connect(self.Pages.setCurrentIndex);self.Pages.currentChanged.connect(self.SelectionChanged)
        self.Bridge.StateChanged.connect(self.Refresh);self.Bridge.ProjectAdded.connect(self.SelectProject);self.Bridge.OperationFailed.connect(self.ShowError)
        self.Navigation.setCurrentRow(0);self.Refresh()
        preferences=self.Catalog.Data.get("preferences",{})
        if preferences.get("remember_window",True) and preferences.get("window_geometry"):
            self.restoreGeometry(QByteArray.fromBase64(preferences["window_geometry"].encode()))
        self.statusBar().showMessage("Ready")

    def Page(self,title,subtitle):
        page=QWidget(self);layout=QVBoxLayout(page);layout.setContentsMargins(24,24,24,20);layout.setSpacing(16)
        layout.addWidget(Heading(title,page));layout.addWidget(Muted(subtitle,page));self.Pages.addWidget(page);return page,layout

    def BuildProjects(self):
        page,layout=self.Page("Projects","Create something new, or continue where you left off.")
        toolbar=QHBoxLayout();self.Search=QLineEdit(page);self.Search.setPlaceholderText("Search projects by name or location…");self.Search.setClearButtonEnabled(True)
        self.AddButton=QPushButton(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogOpenButton),"Add Project…",page)
        self.NewButton=QPushButton(self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogNewFolder),"New Project…",page)
        toolbar.addWidget(self.Search,1);toolbar.addWidget(self.AddButton);toolbar.addWidget(self.NewButton);layout.addLayout(toolbar)
        self.ProjectStack=QStackedWidget(page);self.ProjectTable,self.ProjectModel=Table(["Project","Location","Editor","Last opened"],page)
        self.Proxy=QSortFilterProxyModel(self);self.Proxy.setSourceModel(self.ProjectModel);self.Proxy.setFilterCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive);self.Proxy.setFilterKeyColumn(-1);self.Proxy.setSortRole(SORT_ROLE);self.ProjectTable.setModel(self.Proxy)
        self.ProjectTable.horizontalHeader().setSectionResizeMode(1,QHeaderView.ResizeMode.Stretch);self.ProjectTable.horizontalHeader().setStretchLastSection(False)
        self.ProjectTable.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);self.ProjectTable.customContextMenuRequested.connect(self.ProjectMenu)
        self.ProjectTable.selectionModel().selectionChanged.connect(self.SelectionChanged);self.ProjectTable.doubleClicked.connect(lambda _:self.OpenSelected())
        self.ProjectStack.addWidget(self.ProjectTable)
        empty=QWidget(page);empty_layout=QVBoxLayout(empty);empty_layout.addStretch();self.EmptyTitle=Heading("Your next project starts here",empty);self.EmptyTitle.setAlignment(Qt.AlignmentFlag.AlignCenter);empty_layout.addWidget(self.EmptyTitle)
        self.EmptyText=Muted("Create a project or add an existing .bproject file to your library.",empty);self.EmptyText.setAlignment(Qt.AlignmentFlag.AlignCenter);empty_layout.addWidget(self.EmptyText);empty_layout.addStretch();self.ProjectStack.addWidget(empty);layout.addWidget(self.ProjectStack,1)
        footer=QHBoxLayout();self.ProjectCount=Muted("",page);footer.addWidget(self.ProjectCount,1);footer.addWidget(QLabel("Open with",page));self.LaunchVersion=QComboBox(page);self.LaunchVersion.setMinimumWidth(130);footer.addWidget(self.LaunchVersion)
        self.OpenButton=QPushButton("Open Project",page);footer.addWidget(self.OpenButton);layout.addLayout(footer)
        self.Search.textChanged.connect(self.FilterProjects);self.AddButton.clicked.connect(self.Bridge.AddExistingProject);self.NewButton.clicked.connect(self.NewProject);self.OpenButton.clicked.connect(self.OpenSelected)

    def BuildEditors(self):
        page,layout=self.Page("Editors","Installed engine versions available to open your projects.")
        row=QHBoxLayout();self.EditorsPath=Muted("",page);row.addWidget(self.EditorsPath,1);locate=QPushButton("Locate Editors…",page);refresh=QPushButton("Refresh",page);row.addWidget(locate);row.addWidget(refresh);layout.addLayout(row)
        self.EditorTable,self.EditorModel=Table(["Version","Installation","Project format","Type"],page);self.EditorTable.horizontalHeader().setSectionResizeMode(1,QHeaderView.ResizeMode.Stretch);layout.addWidget(self.EditorTable,1)
        self.EditorNotice=Muted("No editor versions installed. Locate a folder containing version directories and editor.json manifests.",page);layout.addWidget(self.EditorNotice)
        refresh.clicked.connect(self.RescanEditors);locate.clicked.connect(self.LocateEditors)
        self.EditorTable.doubleClicked.connect(self.RevealEditor)

    def BuildPreferences(self):
        page,layout=self.Page("Preferences","Hub settings. The interface follows your operating system’s appearance.")
        group=QGroupBox("Projects",page);form=QFormLayout(group);form.setContentsMargins(16,20,16,16);form.setSpacing(12)
        row=QHBoxLayout();self.ProjectRoot=QLineEdit(str(self.Bridge.ProjectsRoot()),group);browse=QPushButton("Browse…",group);row.addWidget(self.ProjectRoot,1);row.addWidget(browse);form.addRow("Default location",row)
        self.ConfirmRemove=QCheckBox("Confirm before removing a project from the list",group);self.ConfirmRemove.setChecked(self.Catalog.Data.get("preferences",{}).get("confirm_remove",True));form.addRow(self.ConfirmRemove)
        layout.addWidget(group);window=QGroupBox("Window",page);window_layout=QVBoxLayout(window);window_layout.setContentsMargins(16,20,16,16)
        self.RememberWindow=QCheckBox("Remember window size and position",window);self.RememberWindow.setChecked(self.Catalog.Data.get("preferences",{}).get("remember_window",True));window_layout.addWidget(self.RememberWindow);layout.addWidget(window)
        paths=QGroupBox("Storage",page);paths_form=QFormLayout(paths);paths_form.setContentsMargins(16,20,16,16)
        self.StorageFields={}
        for label,path in (("Hub settings",DataPaths.Config()),("Editor installations",self.VersionsRoot())):
            field=QLineEdit(str(path),paths);field.setReadOnly(True);paths_form.addRow(label,field);self.StorageFields[label]=field
        layout.addWidget(paths);layout.addStretch()
        self.ProjectRoot.editingFinished.connect(self.SaveProjectRoot);browse.clicked.connect(self.BrowseProjectRoot)
        self.ConfirmRemove.toggled.connect(lambda value:self.SavePreference("confirm_remove",value));self.RememberWindow.toggled.connect(lambda value:self.SavePreference("remember_window",value))

    def BuildActions(self):
        menu=self.menuBar().addMenu("&File")
        for title,shortcut,callback in (("New Project…",QKeySequence.StandardKey.New,self.NewProject),("Add Project…",QKeySequence.StandardKey.Open,self.Bridge.AddExistingProject)):
            action=QAction(title,self);action.setShortcut(QKeySequence(shortcut));action.triggered.connect(callback);menu.addAction(action)
        self.OpenAction=QAction("Open Selected Project",self);self.OpenAction.triggered.connect(self.OpenSelected);menu.addAction(self.OpenAction)
        menu.addSeparator();exit_action=menu.addAction("Exit");exit_action.triggered.connect(self.close)
        view=self.menuBar().addMenu("&View")
        for index,title in enumerate(("Projects","Editors","Preferences")):
            action=view.addAction(title);action.triggered.connect(lambda checked=False,i=index:self.Navigation.setCurrentRow(i))
        refresh=view.addAction("Refresh Editors");refresh.setShortcut(QKeySequence("F5"));refresh.triggered.connect(self.RescanEditors)
        find=QAction("Find Projects",self);find.setShortcut(QKeySequence.StandardKey.Find);find.triggered.connect(lambda:(self.Navigation.setCurrentRow(0),self.Search.setFocus()));self.addAction(find)
        help_menu=self.menuBar().addMenu("&Help");about=help_menu.addAction("About BAZZALT Hub");about.triggered.connect(lambda:QMessageBox.about(self,"BAZZALT Hub",f"BAZZALT Hub {CURRENT_EDITOR_VERSION}\nProject and editor-version management.\nBuilt with native Qt Widgets."))

    def VersionsRoot(self):
        value=self.Catalog.Data.get("preferences",{}).get("versions_root","");return Path(value) if value else DataPaths.Versions()

    def SelectedProject(self):
        index=self.ProjectTable.currentIndex()
        if not index.isValid() or not self.ProjectTable.selectionModel().hasSelection():return None
        identifier=index.data(ID_ROLE)
        return next((p for p in self.Catalog.Data["projects"] if p["id"]==identifier),None)

    def Refresh(self):
        selected=self.SelectedProject();identifier=selected["id"] if selected else ""
        self.ProjectModel.removeRows(0,self.ProjectModel.rowCount())
        for project in self.Catalog.Data["projects"]:
            date=QDateTime.fromString(project.get("last_opened",""),Qt.DateFormat.ISODate)
            opened=QLocale().toString(date.toLocalTime(),QLocale.FormatType.ShortFormat) if date.isValid() else "Never"
            values=[project["name"],project["path"],project.get("last_editor") or "Automatic",opened];items=[]
            for column,value in enumerate(values):
                item=QStandardItem(value);item.setData(project["id"],ID_ROLE);item.setData(project.get("last_opened","") if column==3 else value.casefold(),SORT_ROLE);item.setToolTip(project["path"]);items.append(item)
            items[0].setIcon(self.windowIcon());font=items[0].font();font.setBold(True);items[0].setFont(font)
            if not Path(project["path"]).is_file():
                for item in items:item.setForeground(self.palette().color(QPalette.ColorRole.PlaceholderText));item.setToolTip("Project file is missing: "+project["path"])
            self.ProjectModel.appendRow(items)
        self.Proxy.sort(3,Qt.SortOrder.DescendingOrder);self.FilterProjects(self.Search.text());self.SelectProject(identifier,clear_filter=False)
        self.EditorModel.removeRows(0,self.EditorModel.rowCount())
        for editor in self.Catalog.Data["editors"]:
            self.EditorModel.appendRow([QStandardItem(str(v)) for v in (editor["version"],editor["root"],editor.get("project_format_max",1),"Development" if editor.get("development") else "Installed")])
        self.EditorsPath.setText(str(self.VersionsRoot()));self.EditorNotice.setVisible(not self.Catalog.Data["editors"]);self.SelectionChanged()
        self.StorageFields["Editor installations"].setText(str(self.VersionsRoot()))

    def FilterProjects(self,text):
        self.Proxy.setFilterFixedString(text);count=self.Proxy.rowCount();total=self.ProjectModel.rowCount();self.ProjectCount.setText(f"{count} of {total} projects" if text else f"{total} project{'s' if total!=1 else ''}")
        self.EmptyTitle.setText("No matching projects" if total else "Your next project starts here");self.EmptyText.setText("Try a different search." if total else "Create a project or add an existing .bproject file to your library.")
        self.ProjectStack.setCurrentIndex(0 if count else 1);self.SelectionChanged()

    def SelectProject(self,identifier,clear_filter=True):
        for row in range(self.ProjectModel.rowCount()):
            index=self.ProjectModel.index(row,0)
            if index.data(ID_ROLE)!=identifier:continue
            if clear_filter:self.Search.clear()
            mapped=self.Proxy.mapFromSource(index)
            if mapped.isValid():self.ProjectTable.setCurrentIndex(mapped);self.ProjectTable.selectRow(mapped.row());self.ProjectTable.scrollTo(mapped)
            return

    def SelectionChanged(self,*_):
        if not hasattr(self,"OpenAction"):return
        project=self.SelectedProject();self.LaunchVersion.clear()
        compatible=self.Catalog.CompatibleEditors(project) if project else []
        for editor in compatible:self.LaunchVersion.addItem(editor["version"],editor["version"])
        if project:
            index=self.LaunchVersion.findData(project.get("last_editor",""))
            if index>=0:self.LaunchVersion.setCurrentIndex(index)
        available=bool(project and compatible and Path(project["path"]).is_file() and self.Pages.currentIndex()==0)
        self.OpenButton.setEnabled(available);self.OpenAction.setEnabled(available);self.LaunchVersion.setEnabled(bool(compatible))

    def OpenSelected(self):
        project=self.SelectedProject()
        if project and self.OpenButton.isEnabled() and self.Bridge.LaunchProject(project["id"],self.LaunchVersion.currentData() or ""):
            self.statusBar().showMessage(f"Launching {project['name']}…",5000)

    def NewProject(self):
        if not self.Catalog.Data["editors"]:
            QMessageBox.information(self,"Editor Required","Locate an installed editor version before creating a project.");self.Navigation.setCurrentRow(1);return
        NewProjectDialog(self.Bridge,self).exec()

    def ProjectMenu(self,position):
        index=self.ProjectTable.indexAt(position)
        if not index.isValid():return
        self.ProjectTable.setCurrentIndex(index);self.ProjectTable.selectRow(index.row());project=self.SelectedProject()
        if not project:return
        menu=QMenu(self);open_action=menu.addAction("Open Project");open_action.setEnabled(self.OpenButton.isEnabled());open_action.triggered.connect(self.OpenSelected)
        versions=menu.addMenu("Open With")
        for editor in self.Catalog.CompatibleEditors(project):
            action=versions.addAction(editor["version"]);action.setEnabled(Path(project["path"]).is_file());action.triggered.connect(lambda checked=False,v=editor["version"]:self.Bridge.LaunchProject(project["id"],v))
        versions.setEnabled(bool(versions.actions()));menu.addSeparator()
        reveal=menu.addAction("Show Project Folder");reveal.triggered.connect(lambda:self.Reveal(Path(project["path"]).parent))
        remove=menu.addAction("Remove From List…");remove.triggered.connect(lambda:self.RemoveProject(project));menu.exec(self.ProjectTable.viewport().mapToGlobal(position))

    def RemoveProject(self,project):
        if self.ConfirmRemove.isChecked() and QMessageBox.question(self,"Remove Project",f"Remove {project['name']} from the Hub?\n\nYour project files will not be deleted.",QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No,QMessageBox.StandardButton.No)!=QMessageBox.StandardButton.Yes:return
        self.Bridge.RemoveProject(project["id"])

    def Reveal(self,path):
        if not path.is_dir() or not QDesktopServices.openUrl(QUrl.fromLocalFile(str(path))):self.ShowError("Could not open this folder: "+str(path))

    def RevealEditor(self,index):
        self.Reveal(Path(self.EditorModel.index(index.row(),1).data()))

    def RescanEditors(self):
        try:self.Catalog.DiscoverEditors(self.VersionsRoot());self.Refresh();self.statusBar().showMessage("Editor installations refreshed",4000)
        except (OSError,ValueError) as error:self.ShowError(str(error))

    def LocateEditors(self):
        folder=QFileDialog.getExistingDirectory(self,"Editor Versions Folder",str(self.VersionsRoot()))
        if folder:self.SavePreference("versions_root",folder);self.RescanEditors()

    def BrowseProjectRoot(self):
        folder=QFileDialog.getExistingDirectory(self,"Default Projects Location",self.ProjectRoot.text())
        if folder:self.ProjectRoot.setText(folder);self.SaveProjectRoot()

    def SaveProjectRoot(self):
        value=self.ProjectRoot.text().strip()
        if not value:self.ProjectRoot.setText(str(self.Bridge.ProjectsRoot()));return
        try:path=Path(value).expanduser().resolve();self.SavePreference("projects_root",str(path));self.ProjectRoot.setText(str(path))
        except (OSError,ValueError) as error:self.ShowError(str(error))

    def SavePreference(self,name,value):
        self.Catalog.Data.setdefault("preferences",{})[name]=value
        try:self.Catalog.Save()
        except (OSError,ValueError) as error:self.ShowError(str(error))

    def ShowError(self,message):
        QMessageBox.warning(QApplication.activeModalWidget() or self,"BAZZALT Hub",message)

    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type()==QEvent.Type.PaletteChange and hasattr(self,"BrandIcon"):
            icon=LogoIcon(self.palette().color(QPalette.ColorRole.WindowText).name());self.setWindowIcon(icon);self.BrandIcon.setPixmap(icon.pixmap(30,30))
            if hasattr(self,"ProjectModel"):
                for row in range(self.ProjectModel.rowCount()):self.ProjectModel.item(row,0).setIcon(icon)

    def closeEvent(self,event):
        if self.RememberWindow.isChecked():self.SavePreference("window_geometry",bytes(self.saveGeometry().toBase64()).decode())
        super().closeEvent(event)
