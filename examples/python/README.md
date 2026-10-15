# Python: подготовка и рендер

```sh
python3 examples/python/render.py build/libladio_core.dylib build/demo/library.json build/python-demo
```

На Linux используйте `.so`, на Windows — DLL. Подготовка и рендер синхронны: в графическом приложении запускайте их в рабочем потоке. Обёртка использует стандартную библиотеку Python и разделяемое ядро. ONNX-анализатор автоматически не подключается.
