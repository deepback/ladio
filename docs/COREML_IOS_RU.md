# Core ML: подключение к iOS

Пакет [LadioCoreML](../bindings/swift/Package.swift) рассчитан на iOS 18+ и macOS 15+.
Добавьте `bindings/swift` как локальный Swift Package и подключите продукт `LadioCoreML`.
Пакет содержит код коннектора, без весов и ресурсов моделей. Модели предоставляет
приложение с сохранением их исходных уведомлений и условий использования.

## Контракты

| Роль | Вход | Выход | Использование |
|---|---|---|---|
| activity | `MelSpectrogram` `[1,400…6000,128,1]` | vocals, drums, bass, others | Активность инструментов и оценка вокальных интервалов |
| structure | `MelSpectrogram` `[1,400…6000,256,1]` | phrases, segments, sections | Свидетельства границ музыкальных фраз |
| downbeat | `input` `[1,4096,131]` | beat/downbeat, tempo, meter | Явный `predict`; приложение проверяет conditioning и декодирует сетку |
| key | `audio` `[1,661504]` | `key_output` | Явный `predict`; приложение интерпретирует классы своего экспорта |

Разделения на стемы и Whisper здесь нет. `activity` не выдаёт звуковые дорожки.
Фронтенд должен совпадать с обучением конкретной модели: одной правильной формы
тензора недостаточно. Изменение автора в метаданных не подтверждает обучение
или право публикации весов. Локальные исследовательские файлы остаются в `private/`.

## Подготовка файлов

Для собственной `.mlmodel`/`.mlpackage` используйте компиляцию в Xcode под целевое
устройство. Скопированный с другой платформы `.mlmodelc` не гарантирует переносимость
на iPhone. Модели добавляются в ресурсы приложения отдельно от Swift Package.

Создание реестра контрольных сумм локальных скомпилированных пакетов:

```sh
python3 scripts/coreml_registry.py /path/to/local/coreml
```

Реестр описывает фактические файлы; он не меняет происхождение или лицензию моделей.
Не обновляйте его после случайного повреждения файлов: восстановите проверенный экспорт.
Downbeat frontend в нём — требование к приложению, а не реализация conditioning.

## Инференс и планировщик

```swift
import Foundation
import LadioCoreML

let registryURL = modelRoot.appendingPathComponent("registry.json")
let registry = try LadioCoreMLRegistry.load(from: registryURL)
let activityModel = registry.models.first { $0.role == .activity }!
let structureModel = registry.models.first { $0.role == .structure }!
let connector = LadioCoreMLConnector(modelRoot: modelRoot)
let observations = try await connector.musicalEvents(
    activity: activityFeatures, structure: structureFeatures,
    frameCount: frames, sourceHash: audioContentHash,
    activityModel: activityModel, structureModel: structureModel,
    frontend: "magnitude-slaney-A-dB-24k-v1",
    deadline: Date().addingTimeInterval(20))
try observations.write(to: observationsURL)
```

Входные features готовит приложение: mono 24 kHz, окно 2048, hop 1200,
Slaney magnitude mel, amplitude dB и A weighting, без нормализации по максимуму.
Это контракт существующего event-анализатора; Swift-пакет не декодирует аудио и не
реализует ресэмплер/FFT. Для переобученного экспорта проверьте контракт на известном
аудио. Полное число кадров — `max(400, ceil(duration/0.05))`: длинные признаки
разбиваются на перекрывающиеся окна до 6000 кадров, до 12000 кадров за один вызов.

Далее вызовите `ladio_apply_model_events(track_json, observations_json, output_json)`
из `LadioCore`. [Пример Swift](../examples/swift/ModelEvents.swift) сохраняет временный
JSON и вызывает API. В track JSON нужны `source_hash`, измеренные beats/downbeats,
tempo, duration, sample_rate и кривые нативных признаков. Результат сохраняет сетку
(кроме общей коррекции дублирующихся downbeats) и DSP-гармонию, добавляет активность
и границы, затем повторно строит cues/sections. Общий постпроцессор применяется и к
ONNX, и к Core ML, включая защиту вокальных фраз.

Несовпадение идентичности аудио, неполное покрытие, другой шаг времени, NaN и
некорректные вероятности отвергаются до записи результата. Новые веса требуют нового
реестра; повторно использовать наблюдения от старых весов нельзя. `source_hash`
должен быть стабильным хешем аудиоконтента, а не ID файла или заглушкой.

## Ресурсы и фон

Actor последовательно выполняет синхронный Core ML inference вне MainActor.
Держится не более одной модели; `musicalEvents` освобождает её при завершении,
ошибке и отмене. По умолчанию разрешены CPU и Neural Engine без GPU; для фонового
режима приложение может выбрать `.cpuOnly`. Эти настройки задают разрешённые
устройства, а не обещают фактическое исполнение на ANE.

Отмена и deadline проверяются перед загрузкой, после неё и между predictions.
Текущий prediction не прерывается; 20 секунд — бюджет проверки, не гарантия latency.
Хост ограничивает количество задач и решает, когда отложить анализ при перегреве.
Аудиоколбэк не ждёт инференса. Тональная модель автоматически не запускается:
штатный DSP-анализ тональности остаётся доступен.

## Проверка

```sh
swift test --package-path bindings/swift
```

Опциональный локальный smoke test двух event-моделей на синтетических признаках:

```sh
LADIO_TEST_COREML_REGISTRY=/path/to/coreml/registry.json \
swift test --package-path bindings/swift --filter testLocalCoreMLEventInference
```

Он проверяет загрузку и выходные тензоры, а не качество сведения. Для принятия на
iPhone нужны запуск собственного экспорта, проверенный audio frontend, прослушивание
реальных пар и измерение полного времени подготовки на устройстве.

API: [MLModel](https://developer.apple.com/documentation/coreml/mlmodel),
[computeUnits](https://developer.apple.com/documentation/coreml/mlmodelconfiguration/computeunits).
