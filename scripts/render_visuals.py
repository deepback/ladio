#!/usr/bin/env python3
"""Экспорт схем из Remotion в MP4, GIF и постеры. Нужны Node.js и FFmpeg."""
from pathlib import Path
import subprocess
import os

root = Path(__file__).resolve().parents[1]
visuals, assets = root / 'visuals', root / 'docs/assets'
for scene, composition in [('spectral', 'Spectral'), ('echo', 'Echo'), ('loop', 'Loop')]:
    movie = assets / f'{scene}.mp4'
    browser = ['--browser-executable=' + os.environ['LADIO_BROWSER']] if os.environ.get('LADIO_BROWSER') else []
    subprocess.run(['npx', 'remotion', 'render', 'src/index.ts', composition, str(movie), '--codec=h264', '--crf=22', '--concurrency=2', '--log=error'] + browser, cwd=visuals, check=True)
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-ss', '3.2', '-i', str(movie), '-frames:v', '1', str(assets / f'{scene}-poster.png')], check=True)
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(movie), '-filter_complex', 'fps=12,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4', '-loop', '0', str(assets / f'{scene}.gif')], check=True)
    print(f'{scene}: MP4, GIF, постер', flush=True)
