"""Build a pinned ARM64 Filament SDK on native GitHub runners only."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def Run(args):subprocess.run(list(map(str,args)),check=True,cwd=ROOT)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--platform',choices=('windows','linux'),required=True);args=parser.parse_args()
    if os.environ.get('GITHUB_ACTIONS')!='true':raise SystemExit('Large dependency builds belong on GitHub Actions')
    work=ROOT/'build/arm64-dependencies';source=work/'filament-source';work.mkdir(parents=True,exist_ok=True)
    if not source.exists():Run(['git','clone','--depth','1','--branch','v1.77.0','https://github.com/google/filament.git',source])
    pin=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if pin!='b01d8a12f283876b337d678e4c74081ae14e20da':raise ValueError('Filament source integrity mismatch')
    sdk=ROOT/'deps/filament';build=work/'build';options=['-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(sdk),'-DDIST_ARCH=aarch64','-DFILAMENT_BUILD_FILAMAT=ON','-DFILAMENT_BUILD_TESTING=OFF','-DSPIRV_SKIP_TESTS=ON','-DFILAMENT_SKIP_SAMPLES=ON','-DFILAMENT_SKIP_SDL2=ON','-DFILAMENT_SUPPORTS_VULKAN=ON','-DFILAMENT_SUPPORTS_WEBGPU=OFF']
    if args.platform=='windows':options+=['-G','Visual Studio 17 2022','-A','ARM64','-DDIST_DIR=aarch64/md','-DUSE_STATIC_CRT=OFF']
    else:options+=['-G','Ninja','-DDIST_DIR=aarch64','-DCMAKE_CXX_FLAGS=-stdlib=libc++','-DCMAKE_C_COMPILER_LAUNCHER=ccache','-DCMAKE_CXX_COMPILER_LAUNCHER=ccache']
    Run(['cmake','-S',source,'-B',build,*options]);Run(['cmake','--build',build,'--config','Release','--target','install','--parallel','2'])
if __name__=='__main__':main()
