"""Compile and execute i386 BlueGL PIC dispatch without building Filament or a GL driver."""
import argparse,subprocess,tempfile
from pathlib import Path
try:from .patch_filament_linux_x86 import Symbols
except ImportError:from patch_filament_linux_x86 import Symbols

def Run(*arguments):subprocess.run([str(value) for value in arguments],check=True)

def Test(source):
    root=Path(source)/'libs/bluegl/src';names=Symbols((root/'BlueGLCoreLinuxImpl.S').read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='bluegl-i386-') as temporary:
        folder=Path(temporary);storage=folder/'storage.c';probe=folder/'probe.c'
        storage.write_text('\n'.join('void* __blue_glCore_'+name+';' for name in names),encoding='utf-8')
        probe.write_text('''#include <assert.h>
#include <string.h>
extern void* __blue_glCore_glGetError;
extern void* __blue_glCore_glClearColor;
extern void* __blue_glCore_glDepthRange;
extern void* __blue_glCore_glGetString;
extern unsigned bluegl_glGetError(void);
extern void bluegl_glClearColor(float,float,float,float);
extern void bluegl_glDepthRange(double,double);
extern const unsigned char* bluegl_glGetString(unsigned);
static unsigned error(void){return 0xfeed;}
static void color(float a,float b,float c,float d){assert(a==.25f&&b==.5f&&c==.75f&&d==1.f);}
static void depth(double a,double b){assert(a==.125&&b==.875);}
static const unsigned char* text(unsigned n){assert(n==7);return (const unsigned char*)"dispatch";}
int main(void){
    __blue_glCore_glGetError=(void*)error; __blue_glCore_glClearColor=(void*)color;
    __blue_glCore_glDepthRange=(void*)depth; __blue_glCore_glGetString=(void*)text;
    for(int i=0;i<10000;++i){
        assert(bluegl_glGetError()==0xfeed);bluegl_glClearColor(.25f,.5f,.75f,1.f);
        bluegl_glDepthRange(.125,.875);assert(!strcmp((const char*)bluegl_glGetString(7),"dispatch"));
    }
    return 0;
}
''',encoding='utf-8')
        Run('clang','-m32','-shared','-fPIC','-Wl,-z,defs,-z,text',root/'BlueGLCoreLinuxUniversalImpl.S',storage,'-o',folder/'libbluegl_probe.so')
        Run('clang','-m32','-fPIE','-pie',probe,'-L'+str(folder),'-lbluegl_probe','-Wl,-rpath,$ORIGIN','-o',folder/'probe')
        Run(folder/'probe')
    print('i386 BlueGL PIC forwarding passed (integer, float, double and pointer arguments/results)')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);Test(parser.parse_args().source)
