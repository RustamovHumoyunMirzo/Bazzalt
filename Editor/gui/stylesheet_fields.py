"""Inspector widgets for native-validated CSS @require declarations."""
import re
from math import ceil,floor
from PySide6.QtGui import QColor
from .widgets import BoolInput,ColorInput,EnumInput,FloatInput,IntInput,StringInput,Vec2Input,Vec3Input,Vec4Input
from ..font_assets import FONT_EXTENSIONS
from .widgets.fields import FieldState

def CssString(value):
    return '"'+str(value).replace('\\','\\\\').replace('"','\\"').replace('\n','\\a ').replace('\r','\\d ')+'"'

def Field(controller,entity_id,property):
    name=property['name'];kind=property['type'];value=property['value'];tr=controller.Window.Localization.Translate
    def commit(literal):
        if controller._updating_inspector or entity_id!=controller.SelectedEntity:return
        accepted=controller._Mutate(tr('gui.edit_style_property'),lambda:controller.Runtime.SetStylesheetProperty(entity_id,name,literal))
        if accepted:controller.SetDirty(True)
        editor.SetFieldState(FieldState.Modified if accepted else FieldState.Error)
        editor.setToolTip(property.get('tooltip','') if accepted else tr('gui.invalid_style_property'))
    if kind in ('texture','font'):
        match=re.search(r'[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}',value);identity=match.group() if match else None
        extensions=FONT_EXTENSIONS if kind=='font' else {'.png','.jpg','.jpeg','.bmp','.tga','.gif','.psd','.hdr','.exr','.btexture'}
        editor=controller._AssetPicker(identity,extensions,lambda identity:commit('asset('+CssString(identity)+')' if identity else 'none'))
    elif kind=='bool':
        editor=BoolInput(value.lower()=='true');editor.ValueChanged.connect(lambda value:commit('true' if value else 'false'))
    elif kind=='color':
        # Native getters return a canonical rgba() expression for this type.
        channels=[float(x) for x in re.findall(r'[-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?',value)]
        color=QColor.fromRgbF(channels[0]/255,channels[1]/255,channels[2]/255,channels[3]) if value.startswith('rgba(') and len(channels)==4 else QColor(value)
        editor=ColorInput(color);editor.ValueChanged.connect(lambda color:commit(f'rgba({color.redF()*255:.9g},{color.greenF()*255:.9g},{color.blueF()*255:.9g},{color.alphaF():.9g})'))
    elif kind=='enum':
        editor=EnumInput();editor.SetOptions(tuple((value,value) for value in property['options']));editor.SetValue(value.strip('"\''));editor.currentIndexChanged.connect(lambda _:commit(CssString(editor.GetValue())))
    elif kind in ('vec2','vec3','vec4'):
        parts=re.findall(r'([-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?)([a-zA-Z%]*)',value)
        editor={'vec2':Vec2Input,'vec3':Vec3Input,'vec4':Vec4Input}[kind]([float(number) for number,unit in parts])
        editor.SetRange(property.get('min',-1e9),property.get('max',1e9))
        editor.ValueChanged.connect(lambda values:commit(' '.join(f'{number:.9g}'+parts[index][1] for index,number in enumerate(values))))
    elif kind in ('float','int','length'):
        match=re.fullmatch(r'\s*([-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?)([a-zA-Z%]*)\s*',value)
        unit=match.group(2) if match else '';number=float(match.group(1)) if match else 0
        minimum=property.get('min',-2147483648 if kind=='int' else -1e9);maximum=property.get('max',2147483647 if kind=='int' else 1e9)
        if kind=='int':editor=IntInput(minimum=ceil(minimum),maximum=floor(maximum),value=int(number));editor.valueChanged.connect(lambda value:commit(str(value)))
        else:
            editor=FloatInput(minimum=minimum,maximum=maximum,value=number,decimals=6)
            if unit:editor.setSuffix(' '+unit)
            editor.valueChanged.connect(lambda value:commit(f'{value:.9g}'+unit))
    else:
        # CSS string values are already validated by native code. Decode only
        # their display spelling; widgets never inject arbitrary declarations.
        text=value[1:-1] if len(value)>=2 and value[0] in ('"',"'") else value
        text=re.sub(r'\\([0-9a-fA-F]{1,6})\s?',lambda match:chr(int(match.group(1),16)),text)
        text=re.sub(r'\\(.)',r'\1',text)
        editor=StringInput(text);editor.editingFinished.connect(lambda:commit(CssString(editor.text())))
    editor.setToolTip(property.get('tooltip',''));editor.SetFieldState(FieldState.Modified if property.get('overridden') else FieldState.Default);return editor
