"""Private surface-shader coverage lowering; never part of public reflection."""
import re

COVERAGE = '''        if (materialParams.bazzaltLodFade.z > 0.5) {
            ivec2 p = ivec2(floor(getNormalizedViewportCoord().xy * getResolution().xy)) & 7;
            int n = 0;
            for (int b = 0; b < 3; ++b) {
                int x = (p.x >> b) & 1; int y = (p.y >> b) & 1;
                n |= ((x ^ y) * 2 + y) << (2 * (2 - b));
            }
            float threshold = (float(n) + 0.5) / 64.0;
            if (threshold < materialParams.bazzaltLodFade.x || threshold >= materialParams.bazzaltLodFade.y) discard;
        }

'''

def _Code(source):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda match:' '*len(match[0]),source)

def _Hook(source):
    code=_Code(source)
    body=re.search(r"void\s+material\s*\(\s*inout\s+MaterialInputs\s+material\s*\)\s*\{",code)
    return re.search(r"prepareMaterial\s*\(\s*material\s*\)\s*;",code[body.end():]) if body else None

def InjectCoverage(translated,reflection):
    if not _Hook(translated):return translated
    if any(parameter['name']=='bazzaltLodFade' for parameter in reflection['parameters']):raise ValueError('Reserved engine parameter name: bazzaltLodFade')
    parameters=re.search(r"parameters\s*:\s*\[",_Code(translated))
    uniform=' { type: float4, name: bazzaltLodFade }'
    if parameters:
        at=parameters.end();suffix=translated[at:];separator='' if _Code(suffix).lstrip().startswith(']') else ','
        translated=translated[:at]+uniform+separator+suffix
    else:
        block=re.search(r"material\s*\{",_Code(translated));at=block.end();translated=translated[:at]+'\n parameters: ['+uniform+'],'+translated[at:]
    code=_Code(translated);body=re.search(r"void\s+material\s*\(\s*inout\s+MaterialInputs\s+material\s*\)\s*\{",code)
    hook=re.search(r"prepareMaterial\s*\(\s*material\s*\)\s*;",code[body.end():]);at=body.end()+hook.end()
    return translated[:at]+'\n'+COVERAGE+translated[at:]
