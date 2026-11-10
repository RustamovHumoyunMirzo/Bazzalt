"""Terrain metadata uses the editor's normal pickers and typed fields."""
from copy import deepcopy
from ..materials import ZERO
from .widgets.collections import ListInput
from .panels.properties import ComponentSection
from .widgets.fields import BoolInput,FloatInput,IntInput,StringInput,Vec2Input,Vec3Input,ColorInput,EnumInput,UIntInput

def TerrainList(controller,entity,name,values):
    tr=controller.Window.Localization.Translate
    defaults={
        'Volumes':lambda:{'Name':tr('terrain.volume'),'Enabled':True,'Operation':1,'Center':(0.,-2.,0.),'Radius':(2.,2.,2.)},
        'Layers':lambda:{'Name':tr('terrain.layer'),'Enabled':True,'Opacity':1.0,'Blend':0,'Heightmap':ZERO,'Splatmap':ZERO,'Material':ZERO,'Color':(1.,1.,1.,1.),'Tiling':1.},
        'Vegetation':lambda:{'Name':tr('terrain.vegetation'),'Enabled':True,'Model':ZERO,'Material':ZERO,'AlignToNormal':True,'Density':.1,'MinHeight':-10000.,'MaxHeight':10000.,'MinSlope':0.,'MaxSlope':45.,'MinScale':.8,'MaxScale':1.2,'Spacing':.5,'Seed':1,'MaxInstances':1000},
        'Splines':lambda:{'Name':tr('terrain.spline'),'Width':1.,'Shoulder':1.,'Depth':0.,'Strength':1.,'Closed':False,'GenerateSurface':False,'SurfaceCollision':True,'SurfaceOffset':.02,'TextureScale':1.,'Material':ZERO,'Color':(1.,1.,1.,1.),'Points':[]},
    }
    def factory(value,changed):
        body=ComponentSection('',removable=False,localization=controller.Window.Localization,subsection=True)
        body.Toggle.parentWidget().hide();body.setObjectName('CollectionBody')
        groups={
            'Vegetation':{'Assets':('Model','Material'), 'Placement':('Density','Spacing','Seed','MaxInstances','AlignToNormal'), 'Height and Slope':('MinHeight','MaxHeight','MinSlope','MaxSlope'), 'Scale':('MinScale','MaxScale')},
            'Layers':{'Composition':('Opacity','Blend'), 'Surface':('Material','Color','Tiling'), 'Maps':('Heightmap','Splatmap')},
            'Splines':{'Profile':('Width','Shoulder','Depth','Strength'), 'Surface':('GenerateSurface','SurfaceCollision','SurfaceOffset','TextureScale','Material','Color')},
        }
        def assign(key,v):value[key]=v;changed(deepcopy(value))
        for key,v in value.items():
            if key.startswith('_') or key in {'InTangents','OutTangents'}:continue
            if key in {'Heightmap','Splatmap','Material','Model'}:
                extensions={'.matinst'} if key=='Material' else {'.gltf','.glb','.obj','.fbx','.filamesh'} if key=='Model' else {'.png','.jpg','.jpeg','.bmp','.tga','.hdr'}
                field=controller._AssetPicker(v,extensions,lambda a,k=key:assign(k,a or ZERO))
            elif key=='Points':
                def point_factory(point,notify):
                    field=Vec3Input(point);field.ValueChanged.connect(notify);return field
                field=ListInput(v,factory=point_factory,default=(0.,0.,0.),maximum=1024,translate=tr,title=tr('terrain.field.Points'),item_name=tr('terrain.point'),add_text=tr('terrain.add_point'));field.ValueChanged.connect(lambda data,k=key:assign(k,data))
            elif key=='Operation':
                field=EnumInput();field.SetOptions([(tr('terrain.volume_add'),0),(tr('terrain.volume_subtract'),1)]);field.SetValue(v);field.currentIndexChanged.connect(lambda _,f=field,k=key:assign(k,f.GetValue()))
            elif key in {'Center','Radius'}:
                field=Vec3Input(v)
                if key=='Radius':
                    for part in field.Inputs:part.setRange(.001,100000)
                field.ValueChanged.connect(lambda data,k=key:assign(k,data))
            elif key=='Blend':
                field=EnumInput();field.SetOptions([(tr('terrain.blend_'+s),i) for i,s in enumerate(('add','replace','maximum','minimum'))]);field.SetValue(v);field.currentIndexChanged.connect(lambda _,f=field,k=key:assign(k,f.GetValue()))
            elif key=='Color':
                field=ColorInput();field.SetValue(v);field.ValueChanged.connect(lambda color,k=key:assign(k,(color.redF(),color.greenF(),color.blueF(),color.alphaF())))
            elif isinstance(v,bool):field=BoolInput(v);field.ValueChanged.connect(lambda data,k=key:assign(k,data))
            elif key=='Seed':field=UIntInput(value=v);field.ValueChanged.connect(lambda data,k=key:assign(k,data))
            elif isinstance(v,int):field=IntInput(0,10000,v);field.valueChanged.connect(lambda data,k=key:assign(k,data))
            elif isinstance(v,float):
                bounds={'Opacity':(0,1),'Tiling':(.001,100000),'Density':(0,1000),'MinHeight':(-100000,100000),'MaxHeight':(-100000,100000),'MinSlope':(0,90),'MaxSlope':(0,90),'MinScale':(.001,1000),'MaxScale':(.001,1000),'Spacing':(.01,100000),'Width':(.001,100000),'Shoulder':(0,100000),'Depth':(-100000,100000),'Strength':(0,1)}
                lo,hi=bounds.get(key,(-100000,100000));field=FloatInput(lo,hi,v);field.valueChanged.connect(lambda data,k=key:assign(k,data))
            else:field=StringInput();field.SetValue(v);field.editingFinished.connect(lambda f=field,k=key:assign(k,f.GetValue()))
            field.setMinimumWidth(0)
            if key=='Points':body.AddWideWidget(field)
            else:
                group=next((group for group,keys in groups.get(name,{}).items() if key in keys),'')
                body.AddField(key,field,group)
                body.SetFieldTitle(key,tr('terrain.field.'+key))
        return body
    kind={'Layers':'layer','Vegetation':'vegetation','Splines':'spline','Volumes':'volume'}[name]
    field=ListInput(values,factory=factory,default=defaults[name],minimum=1 if name=='Layers' else 0,maximum={'Layers':32,'Vegetation':64,'Splines':128,'Volumes':64}[name],translate=tr,title=tr('terrain.field.'+name),item_name=tr('terrain.'+kind),add_text=tr('terrain.add_'+kind))
    def commit(data):
        controller._CommitComponent(entity,'Terrain',name,data)
        if name in {'Layers','Vegetation'}:field.Reindex()
    field.ValueChanged.connect(commit);return field

def TerrainField(controller,entity,name,value):
    if name in {'Layers','Vegetation','Splines','Volumes'}:return TerrainList(controller,entity,name,value)
    commit=lambda v:controller._CommitComponent(entity,'Terrain',name,v)
    if name in {'Resolution','ChunkSize'}:
        field=EnumInput();resolution=controller.Runtime.EntityDetails(entity).get('component_data',{}).get('Terrain',{}).get('Resolution',65)
        choices=(3,5,9,17,33,65,129,257,513,1025,2049) if name=='Resolution' else tuple(n for n in (2,4,8,16,32,64) if n<resolution)
        field.SetOptions([(str(n),n) for n in choices]);field.SetValue(value)
        def changed(_):
            commit(field.GetValue())
            if name=='Resolution':
                from PySide6.QtCore import QSignalBlocker
                data=controller.Runtime.EntityDetails(entity).get('component_data',{}).get('Terrain',{})
                section=controller.Window.Properties._sections.get('runtime.Terrain');chunk=section._fields.get('ChunkSize') if section else None
                if chunk:
                    block=QSignalBlocker(chunk);chunk.SetOptions([(str(n),n) for n in (2,4,8,16,32,64) if n<data.get('Resolution',65)]);chunk.SetValue(data.get('ChunkSize',16));del block
                controller._RefreshInspectorValues()
        field.currentIndexChanged.connect(changed);return field
    if name=='Size':
        field=Vec2Input(value)
        for part in field.Inputs:part.setRange(.001,100000)
        field.ValueChanged.connect(commit);return field
    if name=='VerticalRange':
        field=Vec2Input(value);field.ValueChanged.connect(commit);return field
    if name=='VerticalSamples':
        field=IntInput(4,128,value);field.valueChanged.connect(commit);return field
    if name=='MaxLOD':
        field=IntInput(0,6,value);field.valueChanged.connect(commit);return field
    if name in {'LODDistance','SkirtDepth'}:
        field=FloatInput(.001 if name=='LODDistance' else 0,100000,value);field.valueChanged.connect(commit);return field
    return None
