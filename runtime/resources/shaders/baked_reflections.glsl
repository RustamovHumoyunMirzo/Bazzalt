#if defined(MATERIAL_HAS_LIGHTING) && !defined(VARIANT_DEPTH)
// Intercept only the engine IBL sampler, preserving the native BRDF, AO, clearcoat and SSR blend.
struct BazzaltReflectionSampler {int marker;};
vec4 bazzaltTextureLod(BazzaltReflectionSampler unused,vec3 direction,float lod){
    vec4 original=textureLod(sampler0_iblSpecular,direction,lod);
    if(materialParams.bazzaltLightingEnabled<.5)return original;
    float level=max(frameUniforms.iblRoughnessOneLevel,.00001);float roughness=1.0-sqrt(max(0.0,1.0-clamp(lod/level,0.0,1.0)));
    vec3 worldDirection=normalize(mat3(getUserWorldFromWorldMatrix())*direction);vec4 probe=bazzaltReflection(getUserWorldPosition(),worldDirection,roughness);
    // Native indirect lighting applies its luminance/exposure after evaluating the BRDF.
    vec3 radiance=probe.rgb*getExposure()/max(frameUniforms.iblLuminance,.00000001);
    return vec4(mix(original.rgb,radiance,probe.a),original.a);
}
vec4 bazzaltTextureLod(highp sampler2D s,vec2 d,float l){return textureLod(s,d,l);}
vec4 bazzaltTextureLod(highp sampler2DArray s,vec3 d,float l){return textureLod(s,d,l);}
vec4 bazzaltTextureLod(highp sampler3D s,vec3 d,float l){return textureLod(s,d,l);}
vec4 bazzaltTextureLod(highp samplerCube s,vec3 d,float l){return textureLod(s,d,l);}
float bazzaltTextureLod(highp sampler2DArrayShadow s,vec4 d,float l){return texture(s,d);}
float bazzaltTextureLod(highp sampler2DShadow s,vec3 d,float l){return texture(s,d);}
ivec2 bazzaltTextureSize(BazzaltReflectionSampler unused,int l){return textureSize(sampler0_iblSpecular,l);}
ivec2 bazzaltTextureSize(highp sampler2D s,int l){return textureSize(s,l);}
ivec2 bazzaltTextureSize(highp samplerCube s,int l){return textureSize(s,l);}
ivec3 bazzaltTextureSize(highp sampler2DArray s,int l){return textureSize(s,l);}
ivec3 bazzaltTextureSize(highp sampler3D s,int l){return textureSize(s,l);}
ivec2 bazzaltTextureSize(highp sampler2DShadow s,int l){return textureSize(s,l);}
ivec3 bazzaltTextureSize(highp sampler2DArrayShadow s,int l){return textureSize(s,l);}
ivec2 bazzaltTextureSize(highp usampler2D s,int l){return textureSize(s,l);}
ivec2 bazzaltTextureSize(highp isampler2D s,int l){return textureSize(s,l);}
#define textureSize bazzaltTextureSize
#define textureLod bazzaltTextureLod
#define sampler0_iblSpecular BazzaltReflectionSampler(0)
#endif
