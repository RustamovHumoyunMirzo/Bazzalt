/* Keep implementation/codecs private; no miniaudio types enter the public API. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#if defined(_WIN32)
/* stb_vorbis' stock miniaudio adapter has no wide-path entry point. Keep pull
 * decoding (length/seek/loop/streaming) instead of its callback push fallback. */
static ma_result bazzalt_vorbis_open_w(void* user, const wchar_t* path,
    const ma_decoding_backend_config* config, const ma_allocation_callbacks* allocation,
    ma_data_source** output)
{
    FILE* file;
    ma_stbvorbis* vorbis;
    ma_result result;
    (void)user;
    file = _wfopen(path, L"rb");
    if (file == NULL) return MA_DOES_NOT_EXIST;
    vorbis = (ma_stbvorbis*)ma_malloc(sizeof(*vorbis), allocation);
    if (vorbis == NULL) { fclose(file); return MA_OUT_OF_MEMORY; }
    result = ma_stbvorbis_init_internal(config, vorbis);
    if (result != MA_SUCCESS) { fclose(file); ma_free(vorbis, allocation); return result; }
    vorbis->stb = stb_vorbis_open_file(file, 1, NULL, NULL);
    if (vorbis->stb == NULL) { fclose(file); ma_data_source_uninit(&vorbis->ds); ma_free(vorbis, allocation); return MA_INVALID_FILE; }
    vorbis->usingPushMode = MA_FALSE;
    result = ma_stbvorbis_post_init(vorbis);
    if (result != MA_SUCCESS) { ma_stbvorbis_uninit(vorbis, allocation); ma_free(vorbis, allocation); return result; }
    *output = vorbis;
    return MA_SUCCESS;
}
#endif
/* The resource manager uses VFS callbacks even for real files. Upstream's
 * callback Vorbis adapter uses push mode, which has neither reliable length
 * nor seeking. For our private default-file VFS, duplicate the open file into
 * a FILE stream and retain stb's seekable pull mode. Other callback sources
 * retain the upstream adapter. This code is pinned to miniaudio 0.11.23. */
static ma_result bazzalt_vorbis_open_vfs(void* user, ma_read_proc read, ma_seek_proc seek,
    ma_tell_proc tell, void* context, const ma_decoding_backend_config* config,
    const ma_allocation_callbacks* allocation, ma_data_source** output)
{
    FILE* file = NULL;
    ma_decoder* decoder = (ma_decoder*)context;
    ma_stbvorbis* vorbis;
    ma_result result;
    if (read != ma_decoder_internal_on_read__custom || decoder == NULL ||
        decoder->onRead != ma_decoder__on_read_vfs || decoder->data.vfs.pVFS == NULL ||
        ((ma_vfs_callbacks*)decoder->data.vfs.pVFS)->onRead != ma_default_vfs_read)
        return ma_decoding_backend_init__stbvorbis(user, read, seek, tell, context, config, allocation, output);
#if defined(_WIN32)
    {
        HANDLE duplicate = NULL;
        int descriptor;
        if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)decoder->data.vfs.file,
            GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) return MA_IO_ERROR;
        descriptor = _open_osfhandle((intptr_t)duplicate, _O_RDONLY | _O_BINARY);
        if (descriptor < 0) { CloseHandle(duplicate); return MA_IO_ERROR; }
        file = _fdopen(descriptor, "rb");
        if (file == NULL) { _close(descriptor); return MA_IO_ERROR; }
    }
#else
    {
        int descriptor = dup(fileno((FILE*)decoder->data.vfs.file));
        if (descriptor < 0) return MA_IO_ERROR;
        file = fdopen(descriptor, "rb");
        if (file == NULL) { close(descriptor); return MA_IO_ERROR; }
    }
#endif
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return MA_BAD_SEEK; }
    vorbis = (ma_stbvorbis*)ma_malloc(sizeof(*vorbis), allocation);
    if (vorbis == NULL) { fclose(file); return MA_OUT_OF_MEMORY; }
    result = ma_stbvorbis_init_internal(config, vorbis);
    if (result != MA_SUCCESS) { fclose(file); ma_free(vorbis, allocation); return result; }
    vorbis->stb = stb_vorbis_open_file(file, 1, NULL, NULL);
    if (vorbis->stb == NULL) { fclose(file); ma_data_source_uninit(&vorbis->ds); ma_free(vorbis, allocation); return MA_INVALID_FILE; }
    vorbis->usingPushMode = MA_FALSE;
    result = ma_stbvorbis_post_init(vorbis);
    if (result != MA_SUCCESS) { ma_stbvorbis_uninit(vorbis, allocation); ma_free(vorbis, allocation); return result; }
    *output = vorbis;
    return MA_SUCCESS;
}
void BazzaltInitializeAudioCodecs(void)
{
    g_ma_decoding_backend_vtable_stbvorbis.onInit = bazzalt_vorbis_open_vfs;
#if defined(_WIN32)
    g_ma_decoding_backend_vtable_stbvorbis.onInitFileW = bazzalt_vorbis_open_w;
#endif
}
