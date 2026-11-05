"""Validate platform icon formats and generate launcher rasters during packaging."""
from pathlib import Path
import shutil
import struct


def Validate(path, platform):
    path = Path(path)
    if not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Select an icon file up to 16 MiB")
    data = path.read_bytes()
    if platform == "windows":
        if len(data) < 22 or data[:4] != b"\0\0\1\0":
            raise ValueError("Windows executable icons require an ICO file")
        count = struct.unpack_from("<H", data, 4)[0]
        if not 1 <= count <= 64 or 6 + count * 16 > len(data):
            raise ValueError("Invalid ICO directory")
        for index in range(count):
            size, offset = struct.unpack_from("<II", data, 6 + index * 16 + 8)
            if not size or offset < 6 + count * 16 or offset + size > len(data):
                raise ValueError("Invalid ICO image range")
    elif platform == "macos":
        if len(data) < 8 or data[:4] != b"icns" or struct.unpack_from(">I", data, 4)[0] != len(data):
            raise ValueError("macOS application icons require an ICNS file")
    else:
        from PIL import Image
        if data[:8] != b"\x89PNG\r\n\x1a\n":raise ValueError("Select a square PNG icon")
        with Image.open(path) as image:
            width,height=image.size
            image.verify()
        if width!=height or not 32<=width<=4096:raise ValueError("PNG icon must be square and 32 to 4096 pixels")
        if platform=="android" and width!=512:raise ValueError("Android icon source must be a 512 x 512 PNG")
    return path


def Stage(folder, settings, project):
    platform = settings['target']['platform']
    text = settings['platforms'][platform].get('icon', '')
    if not text:
        return
    path = Path(text)
    if not path.is_absolute():
        path = Path(project).parent / path
    path = Validate(path, platform)
    if platform == "windows":
        from .pe_resources import ApplyIcon
        ApplyIcon(Path(folder) / (settings['production']['name'] + '.exe'), path)
    elif platform == "linux":
        from PIL import Image
        shutil.copy2(path, Path(folder) / 'game-icon.png')
        with Image.open(path) as image:image.convert('RGB').save(Path(folder)/'game-icon.bmp')
        settings['window']['icon'] = 'game-icon.bmp'
    elif platform == "macos":
        shutil.copy2(path, Path(folder).parent / 'Resources/Game.icns')
