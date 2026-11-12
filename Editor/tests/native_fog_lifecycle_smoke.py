"""Opt-in real-driver regression for live fog resource changes.

python -m Editor.tests.native_fog_lifecycle_smoke model.glb --native --seconds 60
Set VK_INSTANCE_LAYERS/VK_LAYER_PATH to enable Vulkan validation. The parent
fails on validation errors as well as crashes, stalled frames and timeouts.
Only a temporary project/model copy is modified; native test windows stay hidden.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model',type=Path)
    parser.add_argument('--native',action='store_true')
    parser.add_argument('--seconds',type=float,default=30)
    parser.add_argument('--backend',default='vulkan')
    parser.add_argument('--model-position',type=float,nargs=3,default=(0,0,0))
    parser.add_argument('--eye',type=float,nargs=3,default=(6,4,8))
    parser.add_argument('--target',type=float,nargs=3,default=(0,0,0))
    parser.add_argument('--worker',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if not args.worker:
        with tempfile.TemporaryFile(mode='w+',encoding='utf-8') as log:
            command=[sys.executable,'-u','-m','Editor.tests.native_fog_lifecycle_smoke',str(args.model.resolve()),'--worker','--seconds',str(args.seconds),'--backend',args.backend,'--model-position',*map(str,args.model_position),'--eye',*map(str,args.eye),'--target',*map(str,args.target)]
            if args.native:command.append('--native')
            try:
                result=subprocess.run(command,cwd=Path(__file__).resolve().parents[2],stdout=log,stderr=subprocess.STDOUT,timeout=max(180,args.seconds+90),env={**os.environ,'PYTHONUTF8':'1'})
            except subprocess.TimeoutExpired:
                log.seek(0);print(log.read());print('Native fog lifecycle timed out');return 1
            log.seek(0);output=log.read();print(output)
        if result.returncode or 'Validation Error:' in output or 'SYNC-HAZARD' in output:
            return 1
        return 0
    return run(args)


def run(args):
    import faulthandler
    import json
    import math
    import shutil
    import uuid
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication,QWidget
    from Editor.runtime import RuntimeService
    faulthandler.enable()
    app=QApplication([]);runtime=RuntimeService();surfaces=[]
    with tempfile.TemporaryDirectory(prefix='bazzalt-fog-lifecycle-') as folder:
        root=Path(folder);(root/'Assets').mkdir()
        shutil.copyfile(args.model,root/'Assets/Model.glb')
        project=root/'Test.bproject'
        project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Fog lifecycle"\nAssetDirectory: Assets\nStartupScene: ""\nProperties:\n',encoding='utf-8')
        try:
            assert runtime.ConfigureRenderingBackend(args.backend)
            assert runtime.LoadProject(project),runtime.LastError()
            model=runtime.InstantiateModelPath(root/'Assets/Model.glb');assert model
            assert runtime.SetTransform(model,args.model_position,(0,0,0,1),(1,1,1))
            camera=runtime.CreateEntity('Game camera');assert runtime.AddComponent(camera,'Camera')
            runtime.SetTransform(camera,args.eye,(0,0,0,1),(1,1,1));runtime.SetGamePreview(camera)
            scene=runtime.SceneInfo()['uuid']
            assert runtime.SetSceneEnvironment(scene,{'fog_enabled':True,'fog_mode':1,'fog_Density':.02,'fog_HeightFalloff':0.})
            ids=(9201,9202,9203);sequences={key:0 for key in ids}
            for key,is_scene,width,height in ((9201,True,960,540),(9202,False,800,450),(9203,False,480,320)):
                if args.native:
                    surface=QWidget();surface.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
                    surface.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
                    surface.resize(width,height);surface.show();app.processEvents();surfaces.append(surface)
                    assert runtime.CreateViewport(key,int(surface.winId()),is_scene,width,height,1)
                else:assert runtime.CreateCapturedViewport(key,is_scene,width,height,1)
            runtime.SetSceneCamera(9201,args.eye,args.target)
            deadline=time.monotonic()+max(90,args.seconds+60)
            def tick():
                assert time.monotonic()<deadline,'GPU presentation deadline exceeded'
                assert runtime.Tick(),runtime.LastError();app.processEvents()
                stats=runtime.Statistics()
                for key in ids:
                    if args.native:sequences[key]=int(stats.get(f'viewport_{key}_frames',0))
                    else:
                        frame=runtime.CapturedViewportFrame(key,sequences[key])
                        if frame:sequences[key]=frame['sequence']
                time.sleep(.01)
                return stats
            while min(sequences.values())<8:tick()
            owner=runtime.CreateEntity('Fog bank');runtime.SetTransform(owner,args.target,(0,0,0,1),(1,1,1))
            started=time.monotonic();action=-1;steady=None;steady_at=0
            while time.monotonic()-started<args.seconds:
                elapsed=time.monotonic()-started;next_action=int(elapsed//2)%6
                if next_action!=action:
                    action=next_action;steady=None;steady_at=time.monotonic()+.8
                    if action==0:
                        assert runtime.AddComponent(owner,'Local Fog')
                        assert runtime.SetComponentProperty(owner,'Local Fog','Size',(20,10,20))
                    elif action==1:assert runtime.SetComponentProperty(owner,'Local Fog','Steps',128)
                    elif action==2:assert runtime.SetComponentEnabled(owner,'Local Fog',False)
                    elif action==3:assert runtime.SetComponentEnabled(owner,'Local Fog',True)
                    elif action==4:assert runtime.RemoveComponent(owner,'Local Fog')
                    print('fog action',action,flush=True)
                eye=tuple(args.eye[i]+math.sin(elapsed*.6+i)*2 for i in range(3))
                runtime.SetSceneCamera(9201,eye,args.target)
                stats=tick()
                if time.monotonic()>steady_at:
                    current=(stats.get('fog_target_creations',0),stats.get('fog_material_creations',0))
                    if steady is not None:assert current==steady,('steady-state GPU allocation churn',steady,current)
                    steady=current
            stats=runtime.Statistics();print('completed',json.dumps(stats),flush=True)
            assert min(sequences.values())>20
            if args.native:
                for key in ids:assert stats.get(f'viewport_{key}_gpu_frame',0)>8,'GPU completion stalled'
        finally:
            started=time.monotonic();runtime.Release();elapsed=time.monotonic()-started
            for surface in surfaces:surface.close()
            app.processEvents();print('shutdown seconds',round(elapsed,3),flush=True)
            assert elapsed<8,'Native shutdown stalled'
    return 0


if __name__=='__main__':raise SystemExit(main())
