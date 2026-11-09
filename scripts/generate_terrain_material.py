"""Generate an embedded terrain variant using the editor's shared shader lowering."""
from pathlib import Path
import sys
import re
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from Editor.terrain_shader import TerrainVariant

if __name__ == '__main__':
    source=TerrainVariant(Path(sys.argv[1]).read_text(encoding='utf-8'))
    if source is None:
        raise ValueError('Expected a surface material')
    root=Path(sys.argv[1]).parent
    source=re.sub(r'#include \"([^\"]+)\"',lambda match:(root/match[1]).read_text(encoding='utf-8'),source)
    Path(sys.argv[2]).write_text(source,encoding='utf-8')
