import React from 'react';
import {AbsoluteFill, Composition, interpolate, useCurrentFrame} from 'remotion';

const A = '#bc553e';
const B = '#237b68';
const INK = '#252721';
const PAPER = '#f5f3eb';
const MUTED = '#71746b';
const font = '-apple-system, BlinkMacSystemFont, "SF Pro Display", "Helvetica Neue", sans-serif';
type Props = { kind: 'spectral' | 'echo' | 'loop' };
const clamp = (x: number) => Math.max(0, Math.min(1, x));
const smooth = (x: number) => { const u = clamp(x); return u * u * (3 - 2 * u); };

// Схематическая волна: не измерение конкретного трека и не звуковой рендер.
const Wave: React.FC<{x: number; y: number; width: number; height: number; color: string; opacity?: number; phase?: number}> = ({x, y, width, height, color, opacity = 1, phase = 0}) => <g opacity={opacity}>{Array.from({length: Math.floor(width / 5)}, (_, i) => {
  const amp = height * (.2 + .5 * Math.abs(Math.sin(i * .21 + phase)) + .3 * Math.abs(Math.sin(i * .79)));
  return <path key={i} d={`M${x + i * 5} ${y - amp / 2}v${amp}`} stroke={color} strokeWidth={2.8} />;
})}</g>;

const Label: React.FC<{x: number; y: number; children: React.ReactNode; size?: number; color?: string; weight?: number}> = ({x, y, children, size = 26, color = INK, weight = 400}) => <text x={x} y={y} fontSize={size} fill={color} fontWeight={weight}>{children}</text>;

export const Transition: React.FC<Props> = ({kind}) => {
  const frame = useCurrentFrame();
  // Паузы в начале/конце позволяют прочитать исходное и конечное состояния.
  const t = clamp(interpolate(frame, [12, 168], [0, 1]));
  const arrival = t >= .76;
  const title = kind === 'spectral' ? 'Обмен спектром' : kind === 'echo' ? 'Эхо и новый вход' : 'Удержание грува';
  const status = arrival ? 'B продолжает в полном диапазоне' : t < .24 ? 'A держит движение, B появляется' : kind === 'echo' ? 'Оригинал уходит, эхо остаётся' : kind === 'loop' ? 'Луп даёт время для развития B' : 'Фильтры освобождают место друг другу';
  const pulse = 120 + 4 * smooth(t / .76);
  return <AbsoluteFill style={{backgroundColor: PAPER, fontFamily: font}}>
    <svg viewBox="0 0 1200 680" width="1200" height="680" style={{fontFamily: font}}>
      <Label x={56} y={76} size={44} weight={570}>{title}</Label>
      <Label x={56} y={122} size={25} color={MUTED}>A — уходящий трек       B — входящий трек</Label>
      {kind === 'spectral' ? <>
        <Label x={56} y={208} color={A}>Трек A</Label>
        <Label x={56} y={372} color={B}>Трек B</Label>
        <rect x={255} y={174} width={888} height={96} fill="#e8dfd2" />
        <rect x={255} y={338} width={888} height={96} fill="#e0e8db" />
        <clipPath id="out"><rect x={255} y={174} width={888 * (1 - smooth((t - .12) / .65))} height={96} /></clipPath>
        <clipPath id="in"><rect x={255 + 888 * (1 - smooth(t / .76))} y={338} width={888 * smooth(t / .76)} height={96} /></clipPath>
        <g clipPath="url(#out)"><Wave x={259} y={222} width={880} height={75} color={A}/></g>
        <g clipPath="url(#in)"><Wave x={259} y={386} width={880} height={75} color={B} phase={2}/></g>
        <Label x={255} y={302} size={22} color={MUTED}>Низкие</Label><Label x={646} y={302} size={22} color={MUTED}>Средние</Label><Label x={1034} y={302} size={22} color={MUTED}>Высокие</Label>
        <Label x={255} y={484} size={26}>{arrival ? 'Фильтр B открыт к сильной доле' : 'A уходит под воду, полоса B расширяется вниз'}</Label>
      </> : kind === 'echo' ? <>
        <Label x={56} y={213} color={A}>Оригинал A</Label>
        <Wave x={285} y={205} width={850} height={66} color={A} opacity={1 - smooth((t - .2) / .25)}/>
        <Label x={56} y={330} color={A}>Эхо A</Label>
        {Array.from({length: 7}, (_, i) => {
          const wet = smooth(t / .28) * (1 - smooth((t - .4 - i * .045) / .4)) * Math.pow(.78, i);
          return <Wave key={i} x={285 + i * 122} y={322} width={104} height={68} color={A} opacity={wet} phase={1}/>;
        })}
        <Label x={56} y={447} color={B}>Новый трек B</Label>
        <Wave x={285} y={439} width={850} height={82} color={B} opacity={smooth(t / .76)} phase={2}/>
        <Label x={285} y={512} size={26}>{arrival ? 'Сухого A уже нет. Короткий хвост завершается.' : 'Посыл в дилей и возврат управляются отдельно'}</Label>
      </> : <>
        <Label x={56} y={204} color={A}>Фрагмент A</Label>
        <rect x={285} y={163} width={190} height={68} fill="#ead7ca"/>
        <Wave x={292} y={197} width={176} height={49} color={A}/>
        <Label x={510} y={204} size={24} color={MUTED}>2 такта с измеренными границами</Label>
        <Label x={56} y={332} color={A}>Удержание</Label>
        {Array.from({length: 4}, (_, i) => <g key={i} opacity={arrival ? 0 : .3 + .7 * clamp(t * 4 - i + 1)}><rect x={285 + i * 190} y={288} width={180} height={68} fill="#ead7ca"/><Wave x={291 + i * 190} y={322} width={168} height={46} color={A}/></g>)}
        {t > .5 && !arrival ? <path d={`M${285 + ((t - .5) / .26) * 760} 282v82`} stroke={INK} strokeWidth={3}/> : null}
        <Label x={56} y={454} color={B}>Развитие B</Label>
        <Wave x={285} y={446} width={850} height={80} color={B} opacity={smooth(t / .76)} phase={2}/>
        <Label x={285} y={518} size={26}>{arrival ? 'Луп закончился. B проходит дальше без повтора.' : 'Цельная петля, затем короткий разгон перед входом'}</Label>
      </>}
      <path d="M56 570H1144" stroke="#d6d6ca"/>
      <path d={`M56 570H${56 + 1088 * t}`} stroke={B} strokeWidth={4}/>
      <Label x={56} y={620} size={26} weight={550}>{status}</Label>
      <Label x={1005} y={620} size={26} color={B}>{pulse.toFixed(1)} BPM</Label>
    </svg>
  </AbsoluteFill>;
};

export const RemotionRoot: React.FC = () => <>
  <Composition id="Spectral" component={Transition} width={1200} height={680} fps={24} durationInFrames={192} defaultProps={{kind: 'spectral'}} />
  <Composition id="Echo" component={Transition} width={1200} height={680} fps={24} durationInFrames={192} defaultProps={{kind: 'echo'}} />
  <Composition id="Loop" component={Transition} width={1200} height={680} fps={24} durationInFrames={192} defaultProps={{kind: 'loop'}} />
</>;
