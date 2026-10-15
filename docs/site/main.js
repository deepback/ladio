'use strict';
const scenes = {
  spectral: { title: 'Схема обмена спектром', text: 'У A постепенно уходят высокие частоты. B появляется узкой полосой и расширяется к своей сильной доле. Фильтры движутся отдельно от громкости.' },
  echo: { title: 'Схема эхо и нового входа', text: 'Сухой сигнал A заканчивается, но его мотив ещё отвечает через дилей. B раскрывается на выбранной фразе, а отфильтрованный хвост постепенно освобождает ему место.' },
  loop: { title: 'Схема удержания грува', text: 'Цельный инструментальный фрагмент A повторяется, пока B развивается. Дробление появляется только перед входом. В назначенной точке луп заканчивается, и B продолжает без возврата назад.' }
};
const video = document.getElementById('scene-video');
const panel = document.getElementById('scene-panel');
const motion = document.getElementById('motion-control');
const tabs = [...document.querySelectorAll('[role="tab"]')];
const reduced = window.matchMedia('(prefers-reduced-motion: reduce)');
let selected = 'spectral';
let wantsMotion = !reduced.matches;
let examples = [];
function syncMotion() {
  if (wantsMotion) video.play().catch(() => { wantsMotion = false; motion.textContent = 'Запустить анимацию'; });
  else video.pause();
  motion.textContent = wantsMotion ? 'Остановить анимацию' : 'Запустить анимацию';
}
function showAudio() {
  const target = document.getElementById('audio-examples');
  const example = examples.find(item => item.scene === selected && item.published === true);
  const labels = { originals: 'Исходные фрагменты', crossfade: 'Обычное наложение', ladio: 'Сведение LADIO' };
  // Публикуется только полный комплект с разрешением на аудио и сведениями о записи.
  if (!example || !example.license || !example.source || !example.level_matching || !Object.keys(labels).every(key => typeof example.audio?.[key] === 'string' && /\.(mp3|ogg|m4a|wav)$/i.test(example.audio[key]) && !example.audio[key].includes('..'))) {
    target.replaceChildren();
    const p = document.createElement('p'); p.className = 'audio-empty'; p.textContent = 'Аудиопример для этой сцены ещё не опубликован.';
    const a = document.createElement('a'); a.href = 'https://github.com/deepback/ladio/blob/main/docs/AUDIO_EXAMPLES.md'; a.textContent = 'Как добавить запись и сравнение';
    target.append(p, a); return;
  }
  target.replaceChildren();
  for (const [key, title] of Object.entries(labels)) {
    const row = document.createElement('div'); row.className = 'audio-item';
    const label = document.createElement('label'); label.htmlFor = `audio-${key}`; label.textContent = title;
    const audio = document.createElement('audio'); audio.id = `audio-${key}`; audio.controls = true; audio.preload = 'none'; audio.src = example.audio[key];
    audio.addEventListener('play', () => target.querySelectorAll('audio').forEach(other => { if (other !== audio) other.pause(); }));
    row.append(label, audio); target.append(row);
  }
  const credit = document.createElement('p'); credit.textContent = `${example.title}. ${example.source}. ${example.license}. ${example.level_matching}`; target.append(credit);
}
function selectScene(tab) {
  selected = tab.dataset.scene;
  tabs.forEach(item => { const active = item === tab; item.setAttribute('aria-selected', String(active)); item.tabIndex = active ? 0 : -1; });
  panel.setAttribute('aria-labelledby', tab.id);
  document.getElementById('scene-description').textContent = scenes[selected].text;
  video.poster = `assets/${selected}-poster.png`;
  video.setAttribute('aria-label', scenes[selected].title);
  video.src = `assets/${selected}.mp4`;
  video.load(); syncMotion(); showAudio();
}
tabs.forEach((tab, index) => {
  tab.addEventListener('click', () => selectScene(tab));
  tab.addEventListener('keydown', event => {
    let next;
    if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
    if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
    if (event.key === 'Home') next = 0;
    if (event.key === 'End') next = tabs.length - 1;
    if (next !== undefined) { event.preventDefault(); tabs[next].focus(); selectScene(tabs[next]); }
  });
});
motion.addEventListener('click', () => { wantsMotion = !wantsMotion; syncMotion(); });
reduced.addEventListener('change', () => { wantsMotion = !reduced.matches; syncMotion(); });
if (reduced.matches) { video.autoplay = false; video.pause(); }
syncMotion();
fetch('audio/examples.json').then(response => { if (!response.ok) throw new Error('examples'); return response.json(); }).then(data => { examples = data.examples || []; showAudio(); }).catch(() => { /* Разметка HTML уже содержит честное пустое состояние. */ });
