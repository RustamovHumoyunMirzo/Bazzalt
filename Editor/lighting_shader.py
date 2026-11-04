"""Engine-owned GI lowering. Public reflection remains the user's shader parameters."""
import re
from ._lighting_sources import HELPER, REFLECTIONS
from .lod_shader import _Code

def InjectLighting(source,reflection):
    configuration=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda match:match[0] if match[0].startswith('"') else ' '*len(match[0]),source)
    if re.search(r'\bshadingModel\s*:\s*"?unlit\b',configuration,re.I):return source
    code=_Code(source)
    definition=re.search(r'\bvoid\s+(material)\s*\(\s*inout\s+MaterialInputs\s+\w+\s*\)\s*\{',code)
    if not definition:return source # post-processing domains have no surface material function
    reserved={'bazzaltLightingData','bazzaltLightingEnabled','bazzaltUserMaterial'}
    if any(p['name'] in reserved for p in reflection['parameters']):raise ValueError('Reserved baked-lighting shader parameter')
    if re.search(r'\bbazzaltUserMaterial\b',code):raise ValueError('Reserved baked-lighting function name')
    # Rename and wrap, so early returns still receive GI after the user's surface setup.
    source=source[:definition.start(1)]+'bazzaltUserMaterial'+source[definition.end(1):]
    code=_Code(source);params=re.search(r'\bparameters\s*:\s*\[',code)
    uniforms=' {type:sampler2d,name:bazzaltLightingData,precision:high},{type:float,name:bazzaltLightingEnabled}'
    if params:
        at=params.end();suffix=source[at:];separator='' if _Code(suffix).lstrip().startswith(']') else ',';source=source[:at]+uniforms+separator+suffix
    else:
        block=re.search(r'\bmaterial\s*\{',code);at=block.end();source=source[:at]+'\nparameters:['+uniforms+'],'+source[at:]
    code=_Code(source);fragment=re.search(r'\bfragment\s*\{',code);depth=1;end=fragment.end()
    while depth and end<len(code):
        depth+=(code[end]=='{')-(code[end]=='}');end+=1
    if depth:raise ValueError('Unbalanced surface fragment block')
    wrapper='\nvoid material(inout MaterialInputs material){bazzaltUserMaterial(material);bazzaltApplyDiffuse(material);}\n'
    source=source[:end-1]+wrapper+REFLECTIONS+'\n'+source[end-1:]
    at=fragment.end();return source[:at]+'\n'+HELPER+'\n'+source[at:]
