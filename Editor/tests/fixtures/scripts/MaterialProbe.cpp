#include <Bazzalt/Script.h>
#include <Bazzalt/Material.h>
COMPONENT(MaterialProbe) {
public:
    PROPERTY(Bazzalt::Material, Surface, {})
    PROPERTY(Bazzalt::Shader, SurfaceShader, {})
    void OnUpdate(float) override {
        if(Surface.IsValid() && Surface.HasParameter("roughness"))Surface.SetFloat("roughness",0.5f);
    }
};
