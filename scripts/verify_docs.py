#!/usr/bin/env python3
"""Проверка публикуемой документации без аудиорендера и ML."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import json
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs'

class Links(HTMLParser):
    def __init__(self):
        super().__init__(); self.links=[]; self.ids=set()
    def handle_starttag(self, tag, attrs):
        a=dict(attrs)
        if 'id' in a: self.ids.add(a['id'])
        for key in ('href','src','poster'):
            if a.get(key): self.links.append(a[key])

def local_target(source, url):
    parts=urlsplit(url)
    if parts.scheme or parts.netloc or not parts.path: return None
    return (source.parent / unquote(parts.path)).resolve()

def verify():
    checked=0
    parser=Links();parser.feed((DOCS/'index.html').read_text())
    for url in parser.links:
        target=local_target(DOCS/'index.html',url)
        if target:
            if not target.is_relative_to(DOCS) or not target.is_file(): raise ValueError(f'Ресурс сайта: {url}')
            checked+=1
        if url.startswith('#') and url[1:] and url[1:] not in parser.ids: raise ValueError(f'Якорь: {url}')
    authored=[ROOT/name for name in ['README.md','README.ru.md','README.en.md','CONTRIBUTING.md','SECURITY.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md']]
    authored+=list(DOCS.rglob('*.md'))+list((ROOT/'examples').rglob('README.md'))+[ROOT/'visuals/README.md',ROOT/'models/beat-this-small0/MODEL_CARD.md']
    for path in authored:
        # Исключаем блоки кода, где пути показывают создаваемые файлы.
        body=re.sub(r'```.*?```','',path.read_text(),flags=re.S)
        for url in re.findall(r'!?\[[^\]]*\]\(([^\s)]+)\)',body):
            target=local_target(path,url)
            if target and (not target.is_relative_to(ROOT) or not target.exists()): raise ValueError(f'{path.relative_to(ROOT)}: {url}')
            checked+=1
    for path in (DOCS/'assets').glob('*.svg'): ET.parse(path)
    entries=json.loads((DOCS/'audio/examples.json').read_text())['examples']
    if {e['scene'] for e in entries} != {'spectral','echo','loop'}: raise ValueError('Неполный список аудиосцен')
    for entry in entries:
        if not entry['published']: continue
        for field in ('source','license','level_matching'):
            if not entry[field]: raise ValueError(f'Нет {field}: {entry["scene"]}')
        for field in ('originals','crossfade','ladio'):
            asset=local_target(DOCS/'index.html',entry['audio'][field])
            if not asset or not asset.is_relative_to(DOCS/'audio') or not asset.is_file(): raise ValueError(f'Аудиопример: {field}')
    for scene in ('spectral','echo','loop'):
        for suffix in ('.mp4','.gif','-poster.png'):
            p=DOCS/'assets'/f'{scene}{suffix}'
            if not p.is_file() or p.stat().st_size<1000: raise ValueError(f'Не готова анимация: {p.name}')
    if 'monospace' in (DOCS/'site/style.css').read_text(): raise ValueError('Декоративный моноширинный текст вернулся')
    print(json.dumps({'passed':True,'links_checked':checked,'animations':3,'audio_published':sum(e['published'] for e in entries)},ensure_ascii=False,indent=2))

if __name__=='__main__': verify()
