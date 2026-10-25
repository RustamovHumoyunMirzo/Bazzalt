"""Normalize flat or wrapped, checksum-verified Filament SDK archives."""
import argparse
from pathlib import Path
import shutil
import tarfile
import tempfile

HEADER=Path('include/filament/Engine.h')

def Normalize(destination):
    destination=Path(destination)
    if (destination/HEADER).is_file():return True
    matches=list(destination.glob('*/include/filament/Engine.h'))
    if len(matches)!=1:return False
    wrapper=matches[0].parents[2]
    entries=list(wrapper.iterdir())
    if any((destination/item.name).exists() for item in entries):raise ValueError('Conflicting SDK layout')
    for item in entries:shutil.move(str(item),str(destination/item.name))
    wrapper.rmdir();return True

def Extract(archive,destination):
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    if any(destination.iterdir()):raise ValueError('SDK destination must be empty')
    with tempfile.TemporaryDirectory(prefix='filament-sdk-',dir=destination.parent) as temporary:
        root=Path(temporary)
        with tarfile.open(archive,'r:gz') as source:source.extractall(root,filter='data')
        if not Normalize(root):raise ValueError('Downloaded Filament package has an unexpected layout')
        for item in root.iterdir():shutil.move(str(item),str(destination/item.name))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('destination',type=Path);parser.add_argument('--archive',type=Path);args=parser.parse_args()
    if args.archive:Extract(args.archive,args.destination)
    elif not Normalize(args.destination):raise SystemExit('No valid Filament SDK found')
