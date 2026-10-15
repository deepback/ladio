# Python integration

`python3 examples/python/render.py build/libladio_core.dylib build/demo/library.json build/python-demo`

Use `.so` on Linux or the DLL on Windows. All preparation/render work is synchronous; call it from a worker in a GUI application. The wrapper uses only Python's standard library and the installed shared core. It does not load the ONNX analyzer implicitly.
