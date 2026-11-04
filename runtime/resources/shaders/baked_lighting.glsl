// Engine-owned baked-lighting contract. This source is embedded for glTF JIT and cooked source materials.
highp vec4 bazzaltFetch(highp float index) {
    ivec2 size=textureSize(materialParams_bazzaltLightingData,0);
    int address=int(index);return texelFetch(materialParams_bazzaltLightingData,ivec2(address%size.x,address/size.x),0);
}
ivec2 bazzaltList(vec3 position,int type) {
    vec4 info=bazzaltFetch(0.0),offsets=bazzaltFetch(1.0);
    int count=int(type==0?info.x:type==1?info.y:info.z);
    if(info.w<0.5)return ivec2(-1,count);
    vec3 cell=(position-bazzaltFetch(2.0).xyz)*bazzaltFetch(3.0).xyz;
    if(any(lessThan(cell,vec3(0.0)))||any(greaterThanEqual(cell,vec3(info.w))))return ivec2(0);
    ivec3 c=ivec3(floor(cell));int grid=int(info.w),at=(c.z*grid+c.y)*grid+c.x;
    vec4 entry=bazzaltFetch(offsets.w+float(at*2+(type==2?1:0)));
    return ivec2(type==1?entry.zw:entry.xy);
}
int bazzaltCandidate(ivec2 list,int index) {if(list.x<0)return index;vec4 value=bazzaltFetch(float(list.x+index/4));return int(value[index%4]);}
vec3 bazzaltBilinear(float offset,int size,vec2 pixel) {
    vec2 at=clamp(pixel,vec2(0.0),vec2(float(size-1)));ivec2 p=ivec2(floor(at));vec2 t=fract(at);
    vec3 a=bazzaltFetch(offset+float(p.y*size+p.x)).rgb,b=bazzaltFetch(offset+float(p.y*size+min(p.x+1,size-1))).rgb;
    vec3 c=bazzaltFetch(offset+float(min(p.y+1,size-1)*size+p.x)).rgb,d=bazzaltFetch(offset+float(min(p.y+1,size-1)*size+min(p.x+1,size-1))).rgb;
    return mix(mix(a,b,t.x),mix(c,d,t.x),t.y);
}
vec3 bazzaltBakedIrradiance(vec3 position,vec3 normal,vec3 geometric) {
    if(materialParams.bazzaltLightingEnabled<0.5)return vec3(0.0);
    vec4 offsets=bazzaltFetch(1.0);ivec2 charts=bazzaltList(position,1);float best=1e30;vec3 surface=vec3(0.0);
    for(int i=0;i<charts.y;++i){int index=bazzaltCandidate(charts,i);float at=offsets.y+float(index*5);vec4 a=bazzaltFetch(at),ab=bazzaltFetch(at+1.0),ac=bazzaltFetch(at+2.0),n=bazzaltFetch(at+3.0),params=bazzaltFetch(at+4.0);
        vec3 delta=position-a.xyz;float plane=abs(dot(delta,n.xyz)),alignment=abs(dot(geometric,n.xyz));if(plane>params.y||alignment<0.1)continue;
        float aa=dot(ab.xyz,ab.xyz),bb=dot(ac.xyz,ac.xyz),crossTerm=dot(ab.xyz,ac.xyz),denominator=aa*bb-crossTerm*crossTerm;if(denominator<1e-16)continue;
        float da=dot(delta,ab.xyz),db=dot(delta,ac.xyz);vec2 uv=vec2(bb*da-crossTerm*db,aa*db-crossTerm*da)/denominator;
        if(uv.x < -0.001 || uv.y < -0.001 || uv.x+uv.y>1.001)continue;
        float score=plane+params.y*(1.0-alignment);if(score<best){best=score;surface=bazzaltBilinear(a.w,int(ab.w),vec2(ac.w,n.w)+uv*params.x);}
    }
    if(best<1e29)return max(surface,vec3(0.0));
    ivec2 list=bazzaltList(position,0);float weights[4];int selected[4];for(int j=0;j<4;++j){weights[j]=0.0;selected[j]=0;}
    for(int i=0;i<list.y;++i){int index=bazzaltCandidate(list,i);vec4 probe=bazzaltFetch(offsets.x+float(index*10));vec3 delta=position-probe.xyz;float d=length(delta);if(d>=probe.w)continue;float t=1.0-d/probe.w,w=t*t/max(.0001,dot(delta,delta));
        for(int j=0;j<4;++j)if(w>weights[j]){for(int k=3;k>0;--k)if(k>j){weights[k]=weights[k-1];selected[k]=selected[k-1];}weights[j]=w;selected[j]=index;break;}}
    float sh[9];sh[0]=.282094792;sh[1]=.488602512*normal.y;sh[2]=.488602512*normal.z;sh[3]=.488602512*normal.x;sh[4]=1.092548431*normal.x*normal.y;sh[5]=1.092548431*normal.y*normal.z;sh[6]=.315391565*(3.0*normal.z*normal.z-1.0);sh[7]=1.092548431*normal.x*normal.z;sh[8]=.546274215*(normal.x*normal.x-normal.y*normal.y);
    vec3 sum=vec3(0.0);float total=0.0;for(int j=0;j<4;++j)if(weights[j]>0.0){vec3 irradiance=vec3(0.0);for(int c=0;c<9;++c)irradiance+=bazzaltFetch(offsets.x+float(selected[j]*10+c+1)).rgb*sh[c];sum+=irradiance*weights[j];total+=weights[j];}return max(vec3(0.0),sum/max(total,.000001));
}
vec3 bazzaltCubeSample(float pixels,int resolution,float maxLevel,vec3 direction,float roughness) {
    vec3 n=normalize(direction),a=abs(n);int face;vec2 uv;
    if(a.x>=a.y&&a.x>=a.z){face=n.x>=0.0?0:1;uv=vec2(n.x>=0.0?-n.z:n.z,-n.y)/a.x;}
    else if(a.y>=a.z){face=n.y>=0.0?2:3;uv=vec2(n.x,n.y>=0.0?n.z:-n.z)/a.y;}
    else{face=n.z>=0.0?4:5;uv=vec2(n.z>=0.0?n.x:-n.x,-n.y)/a.z;}
    float lod=clamp(roughness,0.0,1.0)*maxLevel;int lower=int(lod),upper=min(lower+1,int(maxLevel));vec3 colors[2];
    for(int k=0;k<2;++k){int level=k==0?lower:upper,size=resolution;float offset=pixels;for(int mip=0;mip<level;++mip){offset+=float(6*size*size);size/=2;}colors[k]=bazzaltBilinear(offset+float(face*size*size),size,(uv*.5+.5)*float(size)-.5);}
    return mix(colors[0],colors[1],fract(lod));
}
vec4 bazzaltReflection(vec3 position,vec3 direction,float roughness) {
    if(materialParams.bazzaltLightingEnabled<.5)return vec4(0.0);ivec2 list=bazzaltList(position,2);vec4 offsets=bazzaltFetch(1.0);float weights[4];int selected[4];for(int j=0;j<4;++j){weights[j]=0.0;selected[j]=0;}
    for(int i=0;i<list.y;++i){int index=bazzaltCandidate(list,i);vec4 p=bazzaltFetch(offsets.z+float(index*4));vec3 delta=position-p.xyz;float d=length(delta);if(d>=p.w)continue;float t=1.0-d/p.w,w=t*t/max(.0001,dot(delta,delta));for(int j=0;j<4;++j)if(w>weights[j]){for(int k=3;k>0;--k)if(k>j){weights[k]=weights[k-1];selected[k]=selected[k-1];}weights[j]=w;selected[j]=index;break;}}
    vec3 sum=vec3(0.0);float total=0.0,coverage=0.0;
    for(int j=0;j<4;++j)if(weights[j]>0.0){float at=offsets.z+float(selected[j]*4);vec4 p=bazzaltFetch(at),box=bazzaltFetch(at+1.0),image=bazzaltFetch(at+2.0),blend=bazzaltFetch(at+3.0);vec3 delta=position-p.xyz,d=direction;
        if(image.z>.5&&all(lessThanEqual(abs(delta),box.xyz))){vec3 safe=sign(d)*max(abs(d),vec3(.000001));safe=mix(safe,vec3(.000001),equal(safe,vec3(0.0)));vec3 distance=(sign(safe)*box.xyz-delta)/safe;float t=min(distance.x,min(distance.y,distance.z));d=delta+d*t;}
        vec3 color=bazzaltCubeSample(image.x,int(box.w),image.w,d,roughness)*image.y;sum+=color*weights[j];total+=weights[j];coverage=max(coverage,clamp((p.w-length(delta))/max(.000001,min(blend.x,p.w)),0.0,1.0));
    }
    return vec4(sum/max(total,.000001),coverage);
}

void bazzaltApplyDiffuse(inout MaterialInputs material) {
    if(materialParams.bazzaltLightingEnabled<.5)return;
    mat3 world=mat3(getUserWorldFromWorldMatrix());
    vec3 irradiance=bazzaltBakedIrradiance(getUserWorldPosition(),normalize(world*getWorldNormalVector()),normalize(world*getWorldGeometricNormalVector()));
    float diffuse=1.0;
#if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    diffuse=1.0-max(material.specularColor.r,max(material.specularColor.g,material.specularColor.b));
#elif !defined(SHADING_MODEL_CLOTH) && !defined(SHADING_MODEL_SUBSURFACE)
    diffuse=1.0-material.metallic;
#endif
    // Preserve user emissive exposure weighting while adding physical baked radiance.
    material.emissive.rgb*=mix(1.0/max(getExposure(),.00000001),1.0,material.emissive.w);
    material.emissive.rgb+=material.baseColor.rgb*diffuse*material.ambientOcclusion*irradiance/3.14159265359;
    material.emissive.w=1.0;
}
