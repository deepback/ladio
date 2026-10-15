#!/usr/bin/env python3
"""Воспроизводимые иллюстрации LADIO. Цвет обозначает источник, а не декор."""
from pathlib import Path
from html import escape
import math

ASSETS = Path(__file__).resolve().parents[1] / 'docs/assets'
FONT = '-apple-system,BlinkMacSystemFont,SF Pro Display,SF Pro,Helvetica Neue,Arial,sans-serif'
A, B, INK, MUTED, PAPER = '#bc553e', '#237b68', '#252721', '#71746b', '#f5f3eb'

def svg(name, width, height, title, content, background=PAPER):
    (ASSETS / name).write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title"><title id="title">{escape(title)}</title><style>text{{font-family:{FONT};fill:{INK}}}.muted{{fill:{MUTED}}}</style><rect width="{width}" height="{height}" fill="{background}"/>{content}</svg>''')

def text(x, y, value, size=24, color=INK, weight=400, **attrs):
    extra=' '.join(f'{k.replace("_", "-")}="{v}"' for k,v in attrs.items())
    return f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" style="fill:{color}" {extra}>{escape(value)}</text>'

def wave(x, y, width, height, color, start=0, end=1, phase=0):
    # Схематические амплитуды; это не измерение конкретного трека.
    out=''
    for i in range(int(width/5)):
        u=i/max(1,int(width/5)-1)
        envelope=max(0, min(1,(u-start)*12, (end-u)*12))
        amp=(.22+.58*abs(math.sin(i*.21+phase))+.2*abs(math.sin(i*.79))) * height * envelope
        if amp>0:
            out+=f'<path d="M{x+i*5:.1f} {y-amp/2:.1f}v{amp:.1f}" stroke="{color}" stroke-width="2.8"/>'
    return out

symbol='''<path d="M0 0H53C94 0 105 40 147 40H192V64H146C113 64 99 54 77 37C64 27 53 24 34 24H0Z"/><path d="M0 86H34C62 86 77 72 98 56C110 63 121 67 130 69C115 71 103 83 87 95C72 107 56 110 34 110H0Z"/>'''
content=f'<g transform="translate(72 72) scale(.48)" fill="{INK}">{symbol}</g>'
content+=text(192,119,'LADIO',66,weight=760,letter_spacing='-3')
content+=text(72,236,'Переход — часть музыки.',67,weight=600,letter_spacing='-2')
content+=text(74,292,'Открытый движок автоматического сведения',27,MUTED)
content+=wave(72,418,1060,100,A,end=.72)+wave(72,530,1060,100,B,start=.22,phase=2)
content+=text(74,620,'Старый грув удерживает движение. Новый трек раскрывается в свою фразу.',23,MUTED)
svg('hero.svg',1200,680,'LADIO — открытый движок автоматического сведения',content)

content=text(56,70,'От аудио к музыкальному решению',38,weight=600)
steps=[('Аудио и ритм','PCM → доли, такты, энергия, локальная гармония'),('Материал и сцена','Фразы, точки входа, полезные лупы, стоимость подготовки'),('Общая временная карта','Основные дорожки, отдельные клипы и огибающие'),('Исполнение','Подготовленные окна → фильтры, эхо, реверб → плеер')]
for i,(title,desc) in enumerate(steps):
    y=130+i*100
    content+=f'<path d="M56 {y}H1144" stroke="#d6d6ca"/>'
    content+=text(56,y+43,title,25,weight=550)+text(455,y+43,desc,21,MUTED)
content+=text(56,570,'Анализ и подготовка работают отдельно от аудиопотока.',22,MUTED)
svg('architecture.svg',1200,620,'Четыре последовательных этапа работы LADIO',content)

content=text(56,68,'Как музыка передаёт опору',38,weight=600)
content+=text(56,111,'Условная сцена на 24 такта. Длины подбираются по материалу.',22,MUTED)
x0=280;w=864
for start,label in [(0,'Начало'),(8,'Совместная игра'),(16,'Вход B'),(24,'Продолжение')]:
    x=x0+start*w/24
    content+=text(x,168,label,18,MUTED,text_anchor='end' if start==24 else 'start')
for label,y,s,e,color in [('Ритм и бас A',230,0,16,A),('Новый материал B',330,4,24,B),('Хвост A',430,15,20,A)]:
    content+=text(56,y+7,label,24)
    content+=wave(x0,y,w,64,color,s/24,e/24,1 if color==B else 0)
content+=text(280,516,'Введение',21,MUTED)+text(600,516,'Передача низа',21,MUTED)+text(935,516,'Полный B',21,MUTED)
svg('scene-score.svg',1200,570,'Ритм A, введение B и отдельный затухающий хвост A',content)

content=text(56,68,'Оба трека следуют одному темпу',38,weight=600)
content+=text(56,111,'Пример совместимого сведения: 120 → 124 BPM.',22,MUTED)
for bpm in [120,122,124]:
    y=360-(bpm-120)*42
    content+=f'<path d="M155 {y}H1138" stroke="#d6d6ca"/>'+text(56,y+7,str(bpm),24,MUTED)
points=[]
for i in range(201):
    u=i/200;v=u*u*(3-2*u);points.append(f'{155+u*983:.1f},{360-v*168:.1f}')
content+=f'<polyline points="{" ".join(points)}" fill="none" stroke="{B}" stroke-width="5"/>'
content+=text(155,414,'Начало сцены',22,MUTED)+text(1138,414,'Раскрытие B',22,MUTED,text_anchor='end')
content+=text(56,491,'A постепенно ускоряется',27,A,550)+text(650,491,'B приходит к своей скорости',27,B,550)
content+=text(56,533,'120 BPM → 124 BPM',22,MUTED)+text(650,533,'Сначала 120 BPM, затем 124 BPM',22,MUTED)
svg('tempo-map.svg',1200,585,'Иллюстрация общего темпа и согласованного изменения скорости A и B',content)

content=text(56,78,'Beat This small0',42,weight=600)+text(56,126,'Модель размечает ритм. Планировщик выбирает сведение.',25,MUTED)
content+=text(56,224,'Спектр аудио',27)+text(470,224,'Доли и сильные доли',27,B,550)
content+=f'<path d="M300 215H422m-12-9 12 9-12 9" stroke="{INK}" stroke-width="2" fill="none"/>'
content+=text(56,305,'9,55 МБ',32,weight=600)+text(360,305,'FP32 ONNX',28)+text(780,305,'MIT',28)
content+=text(56,361,'Обучена авторами Beat This. Экспорт и интеграция — LADIO.',22,MUTED)
svg('model-card.svg',1200,415,'Назначение, размер и происхождение модели Beat This small0',content)
print('Пересобраны пять иллюстраций на русском языке.')
