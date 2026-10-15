#!/usr/bin/env python3
"""Original, reproducible SVG figures. No font binaries or remote assets."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs/assets"
FONT = '-apple-system, BlinkMacSystemFont, SF Pro Display, SF Pro, Helvetica Neue, Arial, sans-serif'

def svg(name, width, height, title, content, background="#f7f8fa"):
    (ASSETS / name).write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title">
<title id="title">{title}</title><style>text{{font-family:{FONT};fill:#15171d}}.mono{{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}}.muted{{fill:#717783}}</style>
<rect width="{width}" height="{height}" rx="28" fill="{background}"/>{content}</svg>''')

symbol = '''<path d="M0 0H53C94 0 105 40 147 40H192V64H146C113 64 99 54 77 37C64 27 53 24 34 24H0Z"/><path d="M0 86H34C62 86 77 72 98 56C110 63 121 67 130 69C115 71 103 83 87 95C72 107 56 110 34 110H0Z"/>'''
svg("hero.svg", 1600, 900, "LADIO 1.0.0 — Music between the tracks", f'''
<defs><radialGradient id="glow"><stop stop-color="#5077ff" stop-opacity=".24"/><stop offset="1" stop-color="#5077ff" stop-opacity="0"/></radialGradient><linearGradient id="flow"><stop stop-color="#f9fafc"/><stop offset=".48" stop-color="#9fffd1"/><stop offset="1" stop-color="#87a7ff"/></linearGradient></defs>
<ellipse cx="1100" cy="450" rx="620" ry="480" fill="url(#glow)"/>
<path d="M64 90V64H90M1510 64H1536V90M64 810V836H90M1510 836H1536V810" fill="none" stroke="#8a909d" stroke-width="1"/>
<text x="104" y="112" font-size="18" letter-spacing="3" style="fill:#a8b0c0" class="mono">INDEPENDENT AUDIO RESEARCH</text>
<text x="1496" y="112" font-size="18" text-anchor="end" style="fill:#a8b0c0" class="mono">VOL. 01 / 2026</text>
<g transform="translate(103 250) scale(1.04)" fill="url(#flow)">{symbol}</g>
<text x="350" y="365" font-size="138" font-weight="760" letter-spacing="-7" style="fill:#fff;font-variation-settings:'wght' 760">LADIO</text>
<rect x="811" y="300" width="136" height="50" rx="25" fill="#ffffff" fill-opacity=".07" stroke="#ffffff" stroke-opacity=".18"/>
<text x="879" y="332" text-anchor="middle" font-size="19" class="mono" style="fill:#e1e6ef">1.0.0</text>
<text x="104" y="490" font-size="73" font-weight="500" letter-spacing="-3" style="fill:#fff">Music between</text>
<text x="104" y="575" font-size="73" font-weight="500" letter-spacing="-3" style="fill:#fff">the tracks.</text>
<text x="108" y="651" font-size="23" style="fill:#a8b0c0">A portable engine for musical transition design.</text>
<g stroke="#a1ffd3" fill="none" opacity=".75">''' + ''.join(f'<path d="M1050 {280+i*12} C1190 {310+i*5} 1210 {590-i*5} 1494 {610-i*11}" stroke-width="1.2"/>' for i in range(22)) + '''</g>
<path d="M104 752H1496" stroke="#363c49"/>
<text x="104" y="800" font-size="17" letter-spacing="1.5" class="mono" style="fill:#acb4c4">C++20 · C ABI · ONNX</text>
<text x="1496" y="800" text-anchor="end" font-size="17" letter-spacing="1.5" class="mono" style="fill:#acb4c4">OPEN SOURCE / MIT CORE</text>''', "#101218")

cards = [(65,"01","OBSERVE","Rhythm · energy · harmony"),(440,"02","COMPOSE","Cues · roles · scene choice"),(815,"03","PERFORM","Shared clock · DSP · recovery")]
content = '<text x="65" y="69" font-size="16" letter-spacing="2" class="mono muted">FIGURE 01 / SYSTEM OVERVIEW</text><text x="65" y="134" font-size="43" font-weight="650">One musical plan. Three clear boundaries.</text>'
for x,number,title,subtitle in cards:
    content += f'<rect x="{x}" y="190" width="330" height="198" rx="20" fill="#fff" stroke="#dde1e8"/><text x="{x+25}" y="232" font-size="15" class="mono muted">{number}</text><text x="{x+25}" y="290" font-size="27" font-weight="650">{title}</text><text x="{x+25}" y="334" font-size="17" class="muted">{subtitle}</text>'
for x in [405,780]:content+=f'<path d="M{x} 287h23m-6-6 6 6-6 6" fill="none" stroke="#6478ed" stroke-width="2"/>'
content += '<rect x="65" y="424" width="1080" height="71" rx="14" fill="#eaf0ff"/><text x="91" y="467" font-size="19" fill="#3043b8">Prepare off the audio thread → publish immutable scene → bounded PCM pull</text>'
svg("architecture.svg",1210,550,"LADIO architecture: observe, compose, perform",content)

content = '<text x="65" y="65" font-size="16" letter-spacing="2" class="mono muted">FIGURE 02 / ILLUSTRATIVE SCENE SCORE</text><text x="65" y="127" font-size="40" font-weight="650">A transition has more than one moment.</text>'
x0=245;scale=7.6
for b in range(0,25,4):
    x=x0+b*scale*4
    content += f'<line x1="{x}" x2="{x}" y1="181" y2="506" stroke="#e1e5ec"/><text x="{x}" y="537" text-anchor="middle" class="mono muted" font-size="13">{b:02d}</text>'
rows=[("A · groove",215,0,16,"#101218"),("B · introduction",282,1,13,"#6078ef"),("A · captured tail",349,12,20,"#be83d9"),("B · native",416,16,24,"#148467")]
for label,y,start,end,color in rows:
    content+=f'<text x="65" y="{y+25}" font-size="18">{label}</text><rect x="{x0+start*scale*4}" y="{y}" width="{(end-start)*scale*4}" height="42" rx="10" fill="{color}" fill-opacity=".93"/>'
content+='<line x1="731.4" x2="731.4" y1="168" y2="498" stroke="#147253" stroke-width="2" stroke-dasharray="5 6"/><text x="746" y="183" font-size="14" fill="#147253">B reveals</text><text x="65" y="577" font-size="15" class="muted">Independent introduction · shared tempo · deliberate foundation handoff · decaying return · native continuation</text>'
svg("scene-score.svg",1210,625,"Illustrative shared arrangement and independent musical events",content)

content='<text x="65" y="65" font-size="16" letter-spacing="2" class="mono muted">FIGURE 03 / A SHARED MUSICAL CLOCK</text><text x="65" y="128" font-size="40" font-weight="650">Different source rates. One perceived pulse.</text>'
for bpm in [120,122,124]:
    y=407-(bpm-120)*48
    content+=f'<path d="M150 {y}H1125" stroke="#e0e4eb"/><text x="124" y="{y+5}" text-anchor="end" class="mono muted" font-size="14">{bpm}</text>'
points=[]
for i in range(201):
    u=i/200;tempo=120+4*u*u*(3-2*u);points.append(f'{150+u*975:.1f},{407-(tempo-120)*48:.1f}')
content+=f'<polyline points="{" ".join(points)}" fill="none" stroke="#5f75e8" stroke-width="5" stroke-linecap="round"/>'
content+='<text x="150" y="460" font-size="15" class="mono muted">SCENE START</text><text x="1125" y="460" text-anchor="end" font-size="15" class="mono muted">B ARRIVAL</text><text x="150" y="524" font-size="20">rate A = shared BPM / 120</text><text x="650" y="524" font-size="20">rate B = shared BPM / 124</text><text x="150" y="566" font-size="15" class="muted">Illustrative 120 → 124 BPM, 1:1 correspondence. B reaches its native rate at the planned reveal.</text>'
svg("tempo-map.svg",1210,610,"Illustrative synchronized source rates as a common tempo ramps from 120 to 124 BPM",content)

svg("model-card.svg",1210,420,"Included MIT-licensed Beat This small0 ONNX model",'''
<text x="65" y="65" font-size="16" letter-spacing="2" class="mono muted">MODEL NOTE 01 / OPEN WEIGHTS</text>
<text x="65" y="143" font-size="47" font-weight="650">Small model. Explicit responsibility.</text>
<text x="65" y="201" font-size="23" class="muted">Beat This small0 estimates beats and downbeats.</text>
<text x="65" y="236" font-size="23" class="muted">LADIO decides what to do with them.</text>
<path d="M65 278H1145" stroke="#dce2eb"/>
<text x="65" y="325" font-size="30" font-weight="650">9.55 MB</text><text x="65" y="357" font-size="15" class="mono muted">FP32 ONNX</text>
<text x="410" y="325" font-size="30" font-weight="650">≤ 2 threads</text><text x="410" y="357" font-size="15" class="mono muted">INFERENCE CAP</text>
<text x="850" y="325" font-size="30" font-weight="650">MIT</text><text x="850" y="357" font-size="15" class="mono muted">CODE + PUBLISHED WEIGHTS</text>''')
print("Built 5 original SVG research figures")
