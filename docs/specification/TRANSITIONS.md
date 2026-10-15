# Transition catalogue

Generated IDs and preferred lengths come from `ladio catalog`. This file describes the musical intent and native processor routing. Preferred bars are adapted to source evidence and runway; they are not guaranteed durations for every pair.

| Style ID | Display name | Preferred bars | Role | Native processing |
|---|---|---:|---|---|
| `dj_seamless` | Непрерывный стык | 2 | Continuity / manual | level/entry automation |
| `dj_equal_power` | Равномерное наложение | 4 | Continuity / manual | level/entry automation |
| `dj_late_entry` | Поздний короткий вход | 4 | Continuity / manual | level/entry automation |
| `dj_splice` | Монтаж на границе фразы | 2 | Continuity / manual | level/entry automation |
| `dj_overlap` | Естественное перекрытие | 4 | Continuity / manual | level/entry automation |
| `dj_room_overlap` | Перекрытие с пространством | 4 | Continuity / manual | room/reverb |
| `dj_ring_out` | Сохранить естественный хвост | 4 | Continuity / manual | level/entry automation |
| `dj_room_ring` | Пространственный хвост | 4 | Continuity / manual | room/reverb |
| `dj_short_sweep` | Короткая передача спектра | 2 | Automatic DJ | spectral sweep |
| `dj_phrase_exchange` | Сведение музыкальных фраз | 8 | Automatic DJ | level/entry automation |
| `dj_elastic_entry` | Подведение к сильному входу | 8 | Automatic DJ | tempo delay |
| `dj_spectral_sweep` | Уход под воду и раскрытие | 8 | Automatic DJ | spectral sweep |
| `dj_band_expansion` | Расширение полосы B | 8 | Automatic DJ | spectral sweep |
| `dj_echo_filter` | Фильтр и развивающееся эхо | 8 | Automatic DJ | tempo delay, spectral sweep |
| `dj_room_filter` | Фильтр и пространственный мост | 8 | Automatic DJ | room/reverb, spectral sweep |
| `dj_long_sweep` | Длинная передача спектра | 16 | Automatic DJ | spectral sweep |
| `dj_soft_room` | Мягкое раскрытие через пространство | 10 | Automatic DJ | room/reverb |
| `dj_voice_clearance` | Развести вокальные фразы | 10 | Automatic DJ | room/reverb |
| `dj_rhythm_echo` | Ритмическая фраза в эхо | 4 | Automatic DJ | tempo delay |
| `dj_hook_echo` | Эхо мотива перед новым входом | 4 | Automatic DJ | tempo delay |
| `dj_dance_build` | Длинное развитие с повторителем | 18 | Automatic DJ | room/reverb, capture/repeat, flanger |
| `dj_groove_loop` | Удержание и передача грува | 4 | Automatic DJ | tempo delay, capture/repeat |
| `dj_harmonic_blur` | Повтор и освобождение гармонии | 8 | Automatic DJ | tempo delay, room/reverb, capture/repeat, flanger, rhythmic gate |
| `dj_room_release` | Раскрытие из реверберации | 4 | Automatic DJ | room/reverb |
| `dj_pulse_transition` | Подведение к новому пульсу | 4 | Automatic DJ | tempo delay, room/reverb |

## Musical families

**Spectral exchange and band expansion.** A's spectrum is reduced as B gains useful range. Filter wetness, cutoff and dry gain are separate curves. The incoming attack and full-range arrival are protected anchors; simply starting B on time does not execute the intended spectral preparation.

**Phrase exchange and elastic entry.** Entry runway leads to a selected phrase. Preparation must not use a long envelope to arrive after the actual B event. If needed, independent captured material decorates the interval without moving the main B reader.

**Echo/filter, rhythm echo and hook echo.** Capture/source feed, tempo delay and effect return have separate automation. A short recognizable motif can remain while the dry source leaves. Bass and rhythmic tails are shortened or removed when the new pulse is incompatible.

**Room filter, soft room and room release.** Room/reverb is routed through a send/return with a bounded tail and spectral shaping. Its audible interval may extend past the dry release. Reverb is not implemented by keeping a second dry track quietly running.

**Dance build and groove hold.** A useful measured loop can hold movement while B develops. Repeat stages tighten only near the planned exit. A clear B arrival ends the hold; it must not resume a stale source position after the drop.

**Harmonic blur.** Captured/repeated material, filtering and modulation deliberately reduce harmonic specificity. Delay/room returns provide continuity. The native flanger and gate are scene parameters, not claims of a matching Apple plug-in.

**Voice clearance.** Supplied vocal protection and free space can keep leads apart. The public stereo-first analyzer does not provide studio-quality isolated vocals or guaranteed lyric boundaries; do not interpret this style's name as a source-separation promise.

**Pulse transition.** Introduce suitable non-conflicting material, finish the old pulse and establish the new one at the selected anchor. Half/double-time interpretation is allowed only when musically supported. A long rhythmic echo at the old tempo cannot automatically bridge arbitrary beat grids.

## Processor ownership

| Processor | Implementation | Important controls |
|---|---|---|
| Spectral filter / EQ | `src/core/dj_dsp.hpp`, scene filter graph | HP/LP cutoff, resonance, wetness, EQ curves |
| Tempo delay / echo | `src/core/dj_dsp.hpp`, `dsp.hpp` | Musical delay interval, feed, feedback, filtered return |
| Room / reverb | `src/core/dj_dsp.hpp`, `dsp.hpp` | Predelay, band-dependent decay, send, mix, return, return filtering |
| Flanger | `src/core/dj_dsp.hpp` | Center/depth time, quarter-note modulation period, feedback, mix |
| Rhythmic gate | `src/core/dj_dsp.hpp` | Quarter-note period, duty, depth and softened boundaries |
| Repeat / hold | Scene source-loop/capture graph and compiler | Source range, repeat stages, independent exit anchor |
| Time-stretch | Signalsmith Stretch integration | Shared source/output map, latency context and recovery |

These processors operate in LADIO's native graph. They are not Apple Audio Units and do not require an AU host. Whether a processor is audible on a particular scene depends on its compiled curves, feed material and returns; catalogue presence alone does not establish audibility. The independent-DJ test suite exercises current scene compilation and DSP behavior; older saved-session recipes are retained for compatibility tests rather than advertised as additional automatic styles.
