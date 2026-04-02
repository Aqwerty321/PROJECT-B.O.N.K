import {Circle, Img, Layout, Line, Rect, Txt} from '@motion-canvas/2d';
import type {PossibleVector2, ReferenceReceiver, SignalValue} from '@motion-canvas/core';

import {fonts, palette, stage} from './theme';

function starPosition(index: number) {
  const x = ((Math.sin(index * 91.731) * 0.5 + 0.5) * stage.width) - stage.width / 2;
  const y = ((Math.cos(index * 57.913) * 0.5 + 0.5) * stage.height) - stage.height / 2;
  const size = 1 + ((index * 13) % 3);
  const opacity = 0.16 + ((index * 19) % 60) / 100;
  return {x, y, size, opacity};
}

export function makeBackdrop() {
  const horizontalLines = Array.from({length: 15}, (_, index) => {
    const y = -stage.height / 2 + 70 + index * 70;
    return (
      <Line
        key={`h-${index}`}
        points={[
          [-stage.width / 2, y],
          [stage.width / 2, y],
        ]}
        stroke={'#11243d'}
        lineWidth={1}
        opacity={0.28}
      />
    );
  });

  const verticalLines = Array.from({length: 11}, (_, index) => {
    const x = -stage.width / 2 + 120 + index * 170;
    return (
      <Line
        key={`v-${index}`}
        points={[
          [x, -stage.height / 2],
          [x, stage.height / 2],
        ]}
        stroke={'#0d1e34'}
        lineWidth={1}
        opacity={0.18}
      />
    );
  });

  const stars = Array.from({length: 90}, (_, index) => {
    const star = starPosition(index + 1);
    return (
      <Circle
        key={`star-${index}`}
        position={[star.x, star.y]}
        size={star.size}
        fill={'#dbeafe'}
        opacity={star.opacity}
      />
    );
  });

  return (
    <Layout>
      <Rect size={[stage.width, stage.height]} fill={palette.bg} />
      <Rect size={[stage.width, stage.height]} fill={palette.bgSoft} opacity={0.35} />
      {horizontalLines}
      {verticalLines}
      {stars}
    </Layout>
  );
}

interface HeaderConfig {
  kicker: string;
  title: string;
  subtitle: string;
  width?: number;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
}

export function makeHeader({
  kicker,
  title,
  subtitle,
  width = 1100,
  position = [-780, -420],
  ref,
}: HeaderConfig) {
  return (
    <Layout
      ref={ref}
      layout
      direction={'column'}
      alignItems={'start'}
      gap={16}
      width={width}
      position={position}
    >
      <Txt
        text={kicker.toUpperCase()}
        fontFamily={fonts.mono}
        fontSize={22}
        letterSpacing={9}
        fill={palette.primary}
      />
      <Txt
        text={title}
        fontFamily={fonts.display}
        fontWeight={700}
        fontSize={72}
        lineHeight={82}
        fill={palette.text}
      />
      <Txt
        text={subtitle}
        fontFamily={fonts.body}
        fontSize={30}
        lineHeight={42}
        fill={palette.textMuted}
        width={width}
      />
    </Layout>
  );
}

interface MetricChipConfig {
  label: string;
  value: string;
  accent: string;
  width?: number;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
}

export function makeMetricChip({
  label,
  value,
  accent,
  width = 280,
  position = [0, 0],
  ref,
  opacity = 1,
}: MetricChipConfig) {
  return (
    <Layout ref={ref} position={position} opacity={opacity}>
      <Rect
        width={width}
        height={92}
        radius={20}
        fill={palette.surface}
        stroke={`${accent}77`}
        lineWidth={2}
        shadowColor={`${accent}22`}
        shadowBlur={24}
      >
        <Layout layout direction={'column'} gap={6} padding={18} width={width}>
          <Txt text={label.toUpperCase()} fontFamily={fonts.mono} fontSize={18} letterSpacing={6} fill={palette.textMuted} />
          <Txt text={value} fontFamily={fonts.display} fontSize={34} fontWeight={700} fill={accent} />
        </Layout>
      </Rect>
    </Layout>
  );
}

interface InfoCardConfig {
  eyebrow: string;
  title: string;
  body: string;
  accent: string;
  width: number;
  height?: number;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
}

export function makeInfoCard({
  eyebrow,
  title,
  body,
  accent,
  width,
  height = 180,
  position = [0, 0],
  ref,
  opacity = 1,
}: InfoCardConfig) {
  return (
    <Layout ref={ref} position={position} opacity={opacity}>
      <Rect
        width={width}
        height={height}
        radius={24}
        fill={palette.panel}
        stroke={`${accent}66`}
        lineWidth={2}
        shadowColor={`${accent}1f`}
        shadowBlur={24}
      >
        <Layout layout direction={'column'} gap={12} padding={24} width={width}>
          <Txt text={eyebrow.toUpperCase()} fontFamily={fonts.mono} fontSize={18} letterSpacing={6} fill={accent} />
          <Txt text={title} fontFamily={fonts.display} fontSize={34} fontWeight={700} fill={palette.text} />
          <Txt text={body} fontFamily={fonts.body} fontSize={24} lineHeight={34} fill={palette.textMuted} width={width - 48} />
        </Layout>
      </Rect>
    </Layout>
  );
}

interface ScreenPanelConfig {
  src: string;
  label: string;
  accent: string;
  width: number;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
  imageRef?: ReferenceReceiver<Img>;
  opacity?: number;
}

export function makeScreenPanel({
  src,
  label,
  accent,
  width,
  position = [0, 0],
  ref,
  imageRef,
  opacity = 1,
}: ScreenPanelConfig) {
  const height = width / 1.6;

  return (
    <Layout ref={ref} position={position} opacity={opacity}>
      <Rect
        width={width + 28}
        height={height + 28}
        radius={28}
        fill={palette.panel}
        stroke={`${accent}55`}
        lineWidth={2}
        shadowColor={`${accent}22`}
        shadowBlur={34}
      />
      <Img ref={imageRef} src={src} width={width} height={height} radius={22} />
      <Rect
        width={300}
        height={52}
        radius={18}
        position={[-width / 2 + 182, -height / 2 + 46]}
        fill={'#06101b'}
        stroke={`${accent}88`}
        lineWidth={2}
      >
        <Txt text={label.toUpperCase()} fontFamily={fonts.mono} fontSize={20} letterSpacing={5} fill={accent} />
      </Rect>
    </Layout>
  );
}

interface LeaderConfig {
  points: PossibleVector2[];
  accent: string;
  ref?: ReferenceReceiver<Line>;
  opacity?: number;
}

export function makeLeader({points, accent, ref, opacity = 1}: LeaderConfig) {
  return (
    <Line
      ref={ref}
      points={points}
      stroke={accent}
      lineWidth={4}
      lineDash={[12, 12]}
      end={0}
      opacity={opacity}
    />
  );
}

/* ── Full-bleed screenshot for app-tour scenes ─────────────────────── */

interface FullBleedConfig {
  src: string;
  ref?: ReferenceReceiver<Img>;
  opacity?: number;
}

export function makeFullBleed({src, ref, opacity = 1}: FullBleedConfig) {
  return (
    <Img
      ref={ref}
      src={src}
      width={stage.width}
      height={stage.height}
      opacity={opacity}
    />
  );
}

/* ── Chapter interstitial — centered text on space background ──────── */

interface ChapterCardConfig {
  kicker: string;
  title: string;
  subtitle?: string;
  accent?: string;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
}

export function makeChapterCard({
  kicker,
  title,
  subtitle,
  accent = palette.primary,
  ref,
  opacity = 1,
}: ChapterCardConfig) {
  return (
    <Layout ref={ref} opacity={opacity}>
      {makeBackdrop()}
      <Layout layout direction={'column'} alignItems={'center'} gap={20} width={1200}>
        <Rect width={80} height={3} fill={accent} radius={2} />
        <Txt
          text={kicker.toUpperCase()}
          fontFamily={fonts.mono}
          fontSize={24}
          letterSpacing={12}
          fill={accent}
        />
        <Txt
          text={title}
          fontFamily={fonts.display}
          fontWeight={700}
          fontSize={80}
          lineHeight={96}
          fill={palette.text}
          textAlign={'center'}
          width={1100}
        />
        {subtitle ? (
          <Txt
            text={subtitle}
            fontFamily={fonts.body}
            fontSize={28}
            lineHeight={40}
            fill={palette.textMuted}
            textAlign={'center'}
            width={900}
          />
        ) : null}
        <Rect width={80} height={3} fill={accent} radius={2} marginTop={8} />
      </Layout>
    </Layout>
  );
}

/* ── Callout badge — positioned label that points at a UI area ─────── */

interface CalloutBadgeConfig {
  label: string;
  accent?: string;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
  width?: number;
}

export function makeCalloutBadge({
  label,
  accent = palette.primary,
  position = [0, 0],
  ref,
  opacity = 0,
  width = 360,
}: CalloutBadgeConfig) {
  return (
    <Layout ref={ref} position={position} opacity={opacity}>
      <Rect
        width={width}
        height={56}
        radius={12}
        fill={'#06101bee'}
        stroke={`${accent}aa`}
        lineWidth={2}
        shadowColor={`${accent}44`}
        shadowBlur={20}
      >
        <Txt
          text={label.toUpperCase()}
          fontFamily={fonts.mono}
          fontSize={20}
          letterSpacing={4}
          fill={accent}
        />
      </Rect>
    </Layout>
  );
}

/* ── Stat reveal chip — large value for dramatic metric reveals ────── */

interface StatRevealConfig {
  label: string;
  value: string;
  accent?: string;
  position?: PossibleVector2;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
}

export function makeStatReveal({
  label,
  value,
  accent = palette.accent,
  position = [0, 0],
  ref,
  opacity = 0,
}: StatRevealConfig) {
  return (
    <Layout ref={ref} position={position} opacity={opacity}>
      <Rect
        width={420}
        height={120}
        radius={20}
        fill={'#06101bee'}
        stroke={`${accent}88`}
        lineWidth={2}
        shadowColor={`${accent}33`}
        shadowBlur={30}
      >
        <Layout layout direction={'column'} gap={8} padding={22} width={420} alignItems={'center'}>
          <Txt text={label.toUpperCase()} fontFamily={fonts.mono} fontSize={18} letterSpacing={6} fill={palette.textMuted} />
          <Txt text={value} fontFamily={fonts.display} fontSize={48} fontWeight={700} fill={accent} />
        </Layout>
      </Rect>
    </Layout>
  );
}

/* ── Bottom bar — persistent context strip at the bottom of app-tour scenes ── */

interface BottomBarConfig {
  text: string;
  accent?: string;
  ref?: ReferenceReceiver<Layout>;
  opacity?: number;
}

export function makeBottomBar({
  text,
  accent = palette.primary,
  ref,
  opacity = 0,
}: BottomBarConfig) {
  return (
    <Layout ref={ref} position={[0, stage.height / 2 - 48]} opacity={opacity}>
      <Rect
        width={stage.width}
        height={72}
        fill={'#040814ee'}
      >
        <Txt
          text={text}
          fontFamily={fonts.body}
          fontSize={26}
          lineHeight={36}
          fill={palette.textMuted}
          textAlign={'center'}
          width={stage.width - 120}
        />
      </Rect>
    </Layout>
  );
}

/* ── Scan line — horizontal sweep effect for dramatic reveals ───────── */

interface ScanLineConfig {
  ref?: ReferenceReceiver<Rect>;
  accent?: string;
}

export function makeScanLine({ref, accent = palette.primary}: ScanLineConfig) {
  return (
    <Rect
      ref={ref}
      width={stage.width + 40}
      height={3}
      fill={accent}
      opacity={0.7}
      position={[0, -stage.height / 2 - 10]}
      shadowColor={accent}
      shadowBlur={30}
    />
  );
}
