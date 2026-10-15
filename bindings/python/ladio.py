"""Standard-library ctypes facade for non-real-time preparation/rendering.

Pass an explicit shared-library path. This wrapper does not implement an audio device
or promise real-time Python callback execution.
"""
import ctypes as C
import os

class Ladio:
    def __init__(self, library):
        self._lib = C.CDLL(os.fspath(library))
        self._lib.ladio_version.argtypes = []
        self._lib.ladio_version.restype = C.c_char_p
        self._lib.ladio_last_error.argtypes = []
        self._lib.ladio_last_error.restype = C.c_char_p
        self._lib.ladio_prepare_library.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_uint64]
        self._lib.ladio_prepare_library.restype = C.c_int
        self._lib.ladio_render_session.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p]
        self._lib.ladio_render_session.restype = C.c_int

    @property
    def version(self):
        return self._lib.ladio_version().decode("utf-8")

    def _check(self, result):
        if not result:
            raise RuntimeError(self._lib.ladio_last_error().decode("utf-8", errors="replace"))

    def prepare(self, library_json, output_directory, mode="dj", seed=42):
        self._check(self._lib.ladio_prepare_library(os.fsencode(library_json),
                    os.fsencode(output_directory), mode.encode("utf-8"), seed))

    def render(self, session_json, wav, report):
        self._check(self._lib.ladio_render_session(os.fsencode(session_json), os.fsencode(wav), os.fsencode(report)))
