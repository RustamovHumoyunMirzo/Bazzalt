#include "Rendering/BuiltinPostProcess.h"

#include "gaussian_blur_filamat.h"
#include "vignette_filamat.h"

namespace Bazzalt::Runtime {

EmbeddedShader GetEmbeddedPostProcessShader(UUID id) {
    if (id == GaussianBlurShaderId)
        return {Embedded::GaussianBlurFilamat, Embedded::GaussianBlurFilamatSize};
    if (id == VignetteShaderId)
        return {Embedded::VignetteFilamat, Embedded::VignetteFilamatSize};
    return {};
}

} // namespace Bazzalt::Runtime
