# Сторонние компоненты

Оригинальное ядро, обёртки, документация и иллюстрации LADIO распространяются по MIT. Эта лицензия не заменяет условия сторонних компонентов.

| Компонент | Что включено | Лицензия | Источник / уведомление |
|---|---|---|---|
| Beat This `small0` | FP32 ONNX, таблица фронтенда, метаданные экспорта | MIT для кода и опубликованных весов | [CPJKU/beat_this](https://github.com/CPJKU/beat_this), [включённая лицензия](models/beat-this-small0/LICENSE) |
| Signalsmith Stretch | Заголовки | MIT | [Источник](https://github.com/Signalsmith-Audio/signalsmith-stretch), `third_party/signalsmith/LICENSE.txt` |
| Signalsmith Linear | Заголовки | MIT | [Источник](https://github.com/Signalsmith-Audio/linear), `third_party/signalsmith-linear/LICENSE.txt` |
| nlohmann/json | Один заголовок | MIT | [Источник](https://github.com/nlohmann/json), `third_party/json.LICENSE.MIT` |
| SoXR 0.1.3 | Полный исходный код, отдельная библиотека дополнительного анализатора | LGPL-2.1-or-later и уведомления подкомпонентов | [Источник](https://github.com/chirlu/soxr), `third_party/soxr/LICENCE`, `COPYING.LGPL` |
| ONNX Runtime | Внешняя зависимость без включённых бинарников | MIT | [Источник](https://github.com/microsoft/onnxruntime); сохраните уведомления своей поставки |
| librosa | Источник параметров фронтенда; Python не нужен при исполнении | ISC | [Источник](https://github.com/librosa/librosa), `licenses/Librosa-ISC.txt` |
| Remotion | Зависимости отдельного проекта иллюстраций `visuals`; не звукового SDK | Remotion License | [Условия](https://www.remotion.dev/license), [исходники](https://github.com/remotion-dev/remotion). Исполняемые пакеты в Git не включены |
| React | Зависимость проекта иллюстраций | MIT | [Источник](https://github.com/facebook/react) |

SoXR взят из коммита `945b592b70470e29f917f4de89b4281fbbd540c0` с локальными правками совместимости сборки. Полный исходный код включён. В дополнительном анализаторе сохраняйте заменяемость SoXR; для статической/мобильной поставки нужны соответствующие условия перелинковки LGPL. Ядро не линкует SoXR и ONNX Runtime.

ONNX-экспорт Beat This адаптирует attention к ограниченным плиткам запросов, сохраняя полный контекст checkpoint, точность FP32 и контракт выходов. Численные значения в `beat-model.json` проверяют экспорт, а не музыкальную точность.

Apple-фреймворки, приватные состояния AU, веса Apple, коммерческое аудио, Lane APK, данные аккаунта и диагностика устройства не включены. Apple Music AutoMix — референс продукта, а не зависимость или одобрение. SF Pro используется как установленный системный шрифт с весом 760; файлы шрифтов не распространяются.

Оригинальные файлы лицензий и сторонний исходный код сохранены на языке авторов. Весь репозиторий не следует описывать как исключительно MIT: дополнительные зависимости имеют собственные условия. Самостоятельное ядро и наша авторская часть доступны по MIT.
