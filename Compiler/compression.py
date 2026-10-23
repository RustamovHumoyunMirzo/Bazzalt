"""Windows system XPRESS-Huffman codec; no codec DLL is redistributed."""
import ctypes
import os


class Compressor:
    def __init__(self):
        self.handle = ctypes.c_void_p()
        self.api = None
        if os.name == 'nt':
            self.api = ctypes.WinDLL('cabinet', use_last_error=True)
            self.api.CreateCompressor.argtypes = [ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            self.api.CreateCompressor.restype = ctypes.c_bool
            self.api.Compress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
            self.api.Compress.restype = ctypes.c_bool
            self.api.CloseCompressor.argtypes = [ctypes.c_void_p]
            if not self.api.CreateCompressor(4, None, ctypes.byref(self.handle)):
                raise ctypes.WinError(ctypes.get_last_error())

    def Encode(self, data):
        if not self.api or not data:
            return data, False
        size = ctypes.c_size_t()
        self.api.Compress(self.handle, data, len(data), None, 0, ctypes.byref(size))
        if not size.value:
            raise ctypes.WinError(ctypes.get_last_error())
        output = ctypes.create_string_buffer(size.value)
        if not self.api.Compress(self.handle, data, len(data), output, size.value, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        if size.value >= len(data):return data, False
        return output.raw[:size.value], True

    def Close(self):
        if self.handle.value:
            self.api.CloseCompressor(self.handle)
            self.handle.value = None
