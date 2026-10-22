"""Bounded, editor-only world geometry for transform gesture feedback."""
from math import sin,cos,pi
from .gizmos import Vec3,GizmoMode

class TransformTrail:
    def __init__(self,origin:Vec3,radius:float):
        self.Origin=origin;self.Radius=max(.001,radius);self.Points=[origin]
    def Build(self,mode,result):
        lines=[];origin=self.Origin;radius=self.Radius
        def line(a,b,color):lines.append((a.X,a.Y,a.Z,b.X,b.Y,b.Z,*color))
        def marker(p,size,color):
            for axis in (Vec3(1,0,0),Vec3(0,1,0),Vec3(0,0,1)):line(p-axis*size,p+axis*size,color)
        marker(origin,radius*.08,(.7,.75,.8,.85))
        if mode==GizmoMode.Translate:
            end=origin+result.Translation
            if (end-self.Points[-1]).Length()>radius*.005:
                self.Points.append(end)
                if len(self.Points)>256:self.Points=[self.Points[0]]+self.Points[2:]
            for a,b in zip(self.Points,self.Points[1:]):line(a,b,(.3,.8,1,.95))
            line(origin,end,(.3,.8,1,.35));marker(end,radius*.05,(.3,.8,1,1))
        elif mode==GizmoMode.Rotate:
            axis=result.RotationAxis
            if axis.Length()<.001:return lines
            axis=axis.Normalized();reference=Vec3(0,1,0) if abs(axis.Y)<.9 else Vec3(1,0,0);u=axis.Cross(reference).Normalized();v=axis.Cross(u)
            angle=max(-8*pi,min(8*pi,result.RotationRadians));count=max(1,min(192,int(abs(angle)*32)+1))
            points=[origin+(u*cos(angle*i/count)+v*sin(angle*i/count))*radius for i in range(count+1)]
            for a,b in zip(points,points[1:]):line(a,b,(.8,.55,1,.95))
            line(origin,points[0],(.7,.75,.8,.65));line(origin,points[-1],(.8,.55,1,1));marker(points[0],radius*.05,(.7,.75,.8,.85))
        elif mode==GizmoMode.Scale:
            unit=[Vec3(x,y,z)*(radius*.5) for x in (-1,1) for y in (-1,1) for z in (-1,1)]
            factor=result.Scale;scaled=[Vec3(p.X*min(1000,max(.001,factor.X)),p.Y*min(1000,max(.001,factor.Y)),p.Z*min(1000,max(.001,factor.Z))) for p in unit]
            for i in range(8):
                line(origin+unit[i],origin+scaled[i],(1,.75,.25,.6))
                for bit in (1,2,4):
                    j=i^bit
                    if i<j:line(origin+unit[i],origin+unit[j],(.7,.75,.8,.4));line(origin+scaled[i],origin+scaled[j],(1,.75,.25,.95))
        return lines
