"""Private desktop integration. Never invokes a shell with project filenames."""
from pathlib import Path
import sys
import subprocess
from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices


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
