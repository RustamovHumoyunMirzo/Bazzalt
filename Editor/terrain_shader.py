"""Compile terrain coverage variants without changing public shader reflection."""
import re
from .lod_shader import _Code

COVERAGE = """
        vec2 bazzaltRange = vec2(getUV1().x, 1.0 - getUV1().y);
        vec2 bazzaltPixel = floor(getNormalizedViewportCoord().xy * getResolution().xy);
        float bazzaltThreshold = fract(52.9829189 * fract(dot(bazzaltPixel, vec2(0.06711056, 0.00583715))) + mod(floor(getUserTime().x * 60.0), 256.0) * 0.61803398875);
        if (bazzaltThreshold < bazzaltRange.x || bazzaltThreshold >= bazzaltRange.y) discard;
"""


def TerrainVariant(source):
    code = _Code(source)
    body = re.search(r'void\s+(material)\s*\(\s*inout\s+MaterialInputs\s+(\w+)\s*\)\s*\{', code)
    if not body:
        return None
    depth = 1
    end = body.end()
    while end < len(code) and depth:
        if code[end]=='{':depth+=1
        elif code[end]=='}':depth-=1
        end+=1
    if depth:
        raise ValueError('Unterminated surface material function')
    flip = re.search(r'flipUV\s*:\s*(true|false)', code)
    coverage = COVERAGE if flip is None or flip[1]=='true' else COVERAGE.replace('1.0 - getUV1().y','getUV1().y')
    parameter = body[2]
    # Evaluate the original program before coverage discard, preserving texture
    # derivatives and applying coverage even when the program returns early.
    wrapper = '\n    void material(inout MaterialInputs '+parameter+') {\n        bazzaltTerrainMaterial('+parameter+');'+coverage+'    }\n'
    source = source[:body.start(1)]+'bazzaltTerrainMaterial'+source[body.end(1):end]+wrapper+source[end:]
    code = _Code(source)
    requires = re.search(r'requires\s*:\s*\[([^]]*)\]', code)
    if requires:
        if 'uv1' not in [value.strip() for value in requires[1].split(',')]:
            at = requires.start(1)
            separator = ',' if requires[1].strip() else ''
            source = source[:at] + 'uv1' + separator + source[at:]
    else:
        block = re.search(r'material\s*\{', code)
        if not block:
            raise ValueError('Missing surface material configuration')
        at = block.end()
        source = source[:at] + '\n    requires: [uv1],' + source[at:]
    return source
