"""Private desktop integration. Never invokes a shell with project filenames."""
from pathlib import Path
import sys
import subprocess
from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QFileDialog
import os
from dataclasses import dataclass


@dataclass(frozen=True)
class NativeOpenResult:
    Opened: bool
    Application: str | None = None


def _WindowsOpenWithRecent(extension):
    """Read-only, optional Shell history. Never substitute the default handler."""
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
            rf"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\{extension}\OpenWithList") as key:
            order = winreg.QueryValueEx(key, "MRUList")[0]
            if not isinstance(order, str) or not order:return None
            name = winreg.QueryValueEx(key, order[0])[0]
            # The most recent app may be selected again. The key's write time
            # distinguishes a fresh selection from old, unchanged history.
            return (name, winreg.QueryInfoKey(key)[2]) if isinstance(name, str) and name.lower().endswith(".exe") else None
    except OSError:return None


def _WindowsApplicationPath(name):
    import ctypes
    from ctypes import wintypes
    query = ctypes.WinDLL("shlwapi").AssocQueryStringW
    query.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR,
                      wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    query.restype = ctypes.c_long
    # ASSOCF_OPEN_BYEXENAME / ASSOCSTR_EXECUTABLE: resolve this handler, not .cpp.
    size = wintypes.DWORD(32768)
    buffer = ctypes.create_unicode_buffer(size.value)
    if query(2, 2, name, "open", buffer, ctypes.byref(size)) != 0:return None
    try:return str(ValidateApplication(buffer.value))
    except (OSError, ValueError):return None


def OpenWithApplication(path, parent=None):
    """Display Windows' actual app list. The Shell launches the selected file."""
    import ctypes
    from ctypes import wintypes
    from PySide6.QtGui import QGuiApplication
    path = Path(path).resolve()
    if not path.is_file():raise FileNotFoundError(str(path))
    class OPENASINFO(ctypes.Structure):
        _fields_ = [("pcszFile", wintypes.LPCWSTR), ("pcszClass", wintypes.LPCWSTR),
                    ("oaifInFlags", wintypes.DWORD)]
    shell = ctypes.WinDLL("shell32")
    ole = ctypes.WinDLL("ole32")
    shell.SHOpenWithDialog.argtypes = [wintypes.HWND, ctypes.POINTER(OPENASINFO)]
    shell.SHOpenWithDialog.restype = ctypes.c_long
    ole.CoInitializeEx.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    ole.CoInitializeEx.restype = ctypes.c_long
    ole.CoUninitialize.argtypes = [];ole.CoUninitialize.restype = None
    initialized = ole.CoInitializeEx(None, 2)
    if initialized < 0 and initialized != -2147417850:
        raise OSError(f"COM initialization failed: {initialized & 0xffffffff:08x}")
    try:
        before = _WindowsOpenWithRecent(path.suffix.lower())
        # Qt offscreen WIds are synthetic, never pass them to Windows APIs.
        hwnd = int(parent.winId()) if parent is not None and QGuiApplication.platformName().casefold() not in {"offscreen", "minimal", "minimalegl"} else None
        info = OPENASINFO(str(path), None, 4)  # OAIF_EXEC, no registry writes/default changes.
        status = shell.SHOpenWithDialog(hwnd, ctypes.byref(info))
        if status & 0xffffffff in {0x800704c7, 0x80004004}:return NativeOpenResult(False)
        if status != 0:raise OSError(f"Open With failed: {status & 0xffffffff:08x}")
        after = _WindowsOpenWithRecent(path.suffix.lower())
        # The API returns no chosen app. Cache only newly observed Shell history;
        # stale history/default associations could silently open the wrong app.
        application = _WindowsApplicationPath(after[0] if isinstance(after, tuple) else after) if after and after != before else None
        return NativeOpenResult(True, application)
    finally:
        if initialized >= 0:ole.CoUninitialize()


def ValidateApplication(application):
    path = Path(application)
    if not path.is_absolute():raise ValueError("Application path must be absolute")
    if sys.platform == "darwin" and path.suffix.lower() == ".app" and path.is_dir():return path
    if not path.is_file():raise FileNotFoundError(str(path))
    if sys.platform == "win32":
        if path.suffix.lower() != ".exe":raise ValueError("Select an executable application")
    elif not os.access(path, os.X_OK):raise ValueError("Application is not executable")
    return path


def ChooseApplication(parent, localization, extension):
    """Use the OS-native application browser; do not mutate OS file defaults."""
    tr = localization.Translate
    dialog = QFileDialog(parent, tr("assets.open_with_title", extension=extension))
    dialog.setOption(QFileDialog.Option.DontUseNativeDialog, False)
    dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptOpen)
    dialog.setFileMode(QFileDialog.FileMode.ExistingFile)
    if sys.platform == "win32":
        dialog.setNameFilter(tr("assets.application_windows_filter"))
        dialog.setDirectory(os.environ.get("ProgramFiles", "C:/Program Files"))
    elif sys.platform == "darwin":
        dialog.setNameFilter(tr("assets.application_mac_filter"))
        dialog.setOption(QFileDialog.Option.DontUseCustomDirectoryIcons, True)
        dialog.setDirectory("/Applications")
    else:
        dialog.setNameFilter(tr("assets.application_filter"))
        dialog.setDirectory("/usr/bin")
    if dialog.exec() != QFileDialog.DialogCode.Accepted or not dialog.selectedFiles():return None
    return str(ValidateApplication(dialog.selectedFiles()[0]))


def LaunchApplication(application, paths):
    application = ValidateApplication(application)
    paths = [Path(path).resolve() for path in paths]
    for path in paths:
        if not path.is_file():raise FileNotFoundError(str(path))
    if not paths:return
    if sys.platform == "darwin":
        command = ["open", "-a", str(application), "--", *map(str, paths)]
    else:
        command = [str(application), *map(str, paths)]
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if sys.platform == "win32" else {"start_new_session": True}
    subprocess.Popen(command, shell=False, **options)


def RevealFiles(paths) -> None:
    paths=list(dict.fromkeys(Path(path).resolve() for path in paths))
    if not paths:return
    for path in paths:
        if not path.exists():raise FileNotFoundError(str(path))
    if sys.platform=="win32":
        _RevealWindows(paths)
    elif sys.platform=="darwin":
        subprocess.Popen(["open","-R",*[str(path) for path in paths]])
    else:
        # Freedesktop file managers implement ShowItems to reveal AND select.
        try:
            result=subprocess.run(["dbus-send","--session","--print-reply","--dest=org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1","org.freedesktop.FileManager1.ShowItems",
                "array:string:"+",".join(path.as_uri() for path in paths),"string:"],
                capture_output=True,timeout=3,check=False)
            if result.returncode==0:return
        except (OSError,subprocess.TimeoutExpired):pass
        # Older/noncompliant desktops cannot select; still reveal each folder.
        for folder in dict.fromkeys(path.parent for path in paths):
            if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(folder))):raise OSError(str(folder))


def _RevealWindows(paths) -> None:
    import ctypes
    from ctypes import wintypes
    shell=ctypes.WinDLL("shell32");ole=ctypes.WinDLL("ole32")
    pointer=ctypes.c_void_p
    shell.SHParseDisplayName.argtypes=[wintypes.LPCWSTR,pointer,ctypes.POINTER(pointer),wintypes.DWORD,pointer]
    shell.SHParseDisplayName.restype=ctypes.c_long
    shell.ILFindLastID.argtypes=[pointer];shell.ILFindLastID.restype=pointer
    shell.SHOpenFolderAndSelectItems.argtypes=[pointer,wintypes.UINT,ctypes.POINTER(pointer),wintypes.DWORD]
    shell.SHOpenFolderAndSelectItems.restype=ctypes.c_long
    ole.CoTaskMemFree.argtypes=[pointer];ole.CoTaskMemFree.restype=None
    ole.CoInitializeEx.argtypes=[pointer,wintypes.DWORD];ole.CoInitializeEx.restype=ctypes.c_long
    ole.CoUninitialize.argtypes=[];ole.CoUninitialize.restype=None
    initialized=ole.CoInitializeEx(None,2)
    if initialized<0 and initialized!=-2147417850:raise OSError(f"COM initialization failed: {initialized & 0xffffffff:08x}")
    try:
        groups={}
        for path in paths:groups.setdefault(path.parent,[]).append(path)
        for folder,items in groups.items():
            allocated=[]
            def parse(path):
                pidl=pointer();status=shell.SHParseDisplayName(str(path),None,ctypes.byref(pidl),0,None)
                if pidl.value:allocated.append(pidl)
                if status<0:raise OSError(f"Explorer could not resolve {path}: {status & 0xffffffff:08x}")
                return pidl
            try:
                parent=parse(folder)
                children=(pointer*len(items))(*(shell.ILFindLastID(parse(path)) for path in items))
                status=shell.SHOpenFolderAndSelectItems(parent,len(items),children,0)
                if status<0:raise OSError(f"Explorer could not reveal files: {status & 0xffffffff:08x}")
            finally:
                for pidl in allocated:ole.CoTaskMemFree(pidl)
    finally:
        if initialized>=0:ole.CoUninitialize()
