"""Add i386 PIC OpenGL dispatch thunks to pinned Filament's BlueGL library."""
import argparse,re
from pathlib import Path

OLD='#if defined(__aarch64__)'
NEW='#if defined(__i386__)\n#   include "BlueGLCoreLinuxI386Impl.S"\n#elif defined(__aarch64__)'

def Symbols(text):
    names=re.findall(r'^bluegl_(gl\w+):$',text,re.M)
    if not names or len(set(names))!=len(names):raise ValueError('Invalid BlueGL dispatch symbol list')
    return names

def Generate(text):
    # Preserve the original copyright/license. Each tail-call preserves the i386
    # cdecl stack arguments and all callee-saved registers; only EAX is scratch.
    result=[text.split('*/',1)[0]+'*/\n', '# Generated i386 PIC dispatch; Bazzalt Linux compatibility patch.\n.text\n']
    for name in Symbols(text):
        symbol='bluegl_'+name;pc='.Lpc_'+name
        result.append(f'''.p2align 4
.global {symbol}
.type {symbol}, @function
{symbol}:
    .cfi_startproc
    call {pc}
    .cfi_adjust_cfa_offset 4
{pc}:
    popl %eax
    .cfi_adjust_cfa_offset -4
    addl $_GLOBAL_OFFSET_TABLE_+(.-{pc}), %eax
    movl __blue_glCore_{name}@GOT(%eax), %eax
    jmp *(%eax)
    .cfi_endproc
.size {symbol}, .-{symbol}
''')
    result.append('.section .note.GNU-stack,"",@progbits\n');return ''.join(result)

def Patch(source):
    root=Path(source)/'libs/bluegl/src';universal=root/'BlueGLCoreLinuxUniversalImpl.S';original=universal.read_text(encoding='utf-8')
    if original.count(NEW)==1:updated=original
    elif original.count(OLD)==1:updated=original.replace(OLD,NEW)
    else:raise ValueError('Pinned BlueGL architecture selection changed; review upstream')
    upstream=(root/'BlueGLCoreLinuxImpl.S').read_text(encoding='utf-8')
    if len(Symbols(upstream))!=2575:raise ValueError('Pinned BlueGL function count changed; review upstream')
    generated=Generate(upstream);target=root/'BlueGLCoreLinuxI386Impl.S';changed=False
    for path,text in ((universal,updated),(target,generated)):
        if not path.exists() or path.read_text(encoding='utf-8')!=text:path.write_text(text,encoding='utf-8',newline='\n');changed=True
    return changed

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);args=parser.parse_args();Patch(args.source)
