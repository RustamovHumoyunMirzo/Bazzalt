"""Versioned global-event authoring schema. Runtime uses UUIDs and the cooked file."""
from pathlib import Path
import re
import yaml
TYPES=('Boolean','Integer','Number','String','Vec2','Vec3','Vec4','Quaternion','UUID')
def Validate(value):
    if not isinstance(value,dict) or set(value)!={'Version','Name','Arguments'} or type(value['Version']) is not int or value['Version']!=1:raise ValueError('Expected Version: 1, Name and Arguments')
    def Name(text):return isinstance(text,str) and bool(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]{0,127}',text))
    if not Name(value['Name']) or not isinstance(value['Arguments'],list) or len(value['Arguments'])>32:raise ValueError('Invalid event name or argument count')
    seen=set()
    for row in value['Arguments']:
        if not isinstance(row,dict) or set(row)!={'Name','Type'} or not Name(row['Name']) or row['Name'] in seen or row['Type'] not in TYPES:raise ValueError('Invalid or duplicate event argument')
        seen.add(row['Name'])
    return value
def Read(path):
    path=Path(path)
    if path.stat().st_size>1024*1024:raise ValueError('Global Event exceeds 1 MiB')
    return Validate(yaml.safe_load(path.read_text(encoding='utf-8-sig')))
def Write(path,value):
    path=Path(path);text=yaml.safe_dump(Validate(value),sort_keys=False,allow_unicode=True)
    temporary=path.with_name(path.name+'.tmp')
    try:temporary.write_text(text,encoding='utf-8');temporary.replace(path)
    finally:temporary.unlink(missing_ok=True)
