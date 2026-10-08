"""One validated increment policy for gizmos and Object placement commands."""
from math import isfinite,atan2,asin,radians,sin,cos,sqrt
DEFAULT_UNITS={"translation_step":.5,"rotation_step":15.,"scale_step":.1,"translation_sensitivity":1.,"rotation_sensitivity":1.,"scale_sensitivity":1.,"snapping_enabled":False}
def TransformUnits(value=None):
    result=dict(DEFAULT_UNITS)
    if isinstance(value,dict):
        for key,default in result.items():
            if key=="snapping_enabled":result[key]=value.get(key,default) is True;continue
            try:number=float(value.get(key,default))
            except (TypeError,ValueError):continue
            if isfinite(number) and .0001<=number<=10000:result[key]=number
    return result
def SnapValue(value,increment):return round(value/increment)*increment
def SnapRotation(value,step_degrees):
    x,y,z,w=value;length=sqrt(x*x+y*y+z*z+w*w)
    if length<1e-12:return (0.,0.,0.,1.)
    x,y,z,w=(v/length for v in value)
    roll=atan2(2*(w*x+y*z),1-2*(x*x+y*y));pitch=asin(max(-1.,min(1.,2*(w*y-z*x))));yaw=atan2(2*(w*z+x*y),1-2*(y*y+z*z))
    roll,pitch,yaw=(SnapValue(v,radians(step_degrees))*.5 for v in (roll,pitch,yaw))
    sr,cr,sp,cp,sy,cy=sin(roll),cos(roll),sin(pitch),cos(pitch),sin(yaw),cos(yaw)
    return (sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy,cr*cp*cy+sr*sp*sy)
