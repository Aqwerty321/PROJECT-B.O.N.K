import { useEffect, useState, type ReactNode } from 'react';
import { theme } from '../styles/theme';
import { useSound } from '../hooks/useSound';

interface GlassPanelProps {
  title: string;
  children: ReactNode;
  revealIndex?: number;      // stagger order (0-based)
  bootComplete?: boolean;    // only reveal after boot
  style?: React.CSSProperties;
  className?: string;
  noPadding?: boolean;
}

export function GlassPanel({
  title,
  children,
  revealIndex = 0,
  bootComplete = true,
  style,
  className,
  noPadding = false,
}: GlassPanelProps) {
  const [revealed, setRevealed] = useState(false);
  const { play } = useSound();

  useEffect(() => {
    if (!bootComplete) return;
    const timeout = setTimeout(() => {
      setRevealed(true);
      play('panelOpen');
    }, revealIndex * theme.panelRevealDelay);
    return () => clearTimeout(timeout);
  }, [bootComplete, revealIndex, play]);

  return (
    <div
      className={className}
      style={{
        ...theme.glassmorphism,
        clipPath: theme.chamfer.clipPath,
        position: 'relative',
        overflow: 'hidden',
        opacity: revealed ? 1 : 0,
        transform: revealed ? 'translateY(0)' : 'translateY(8px)',
        transition: 'opacity 0.4s ease-out, transform 0.4s ease-out',
        display: 'flex',
        flexDirection: 'column',
        ...style,
      }}
    >
      {/* CRT scanline overlay */}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          background: `repeating-linear-gradient(
            0deg,
            transparent,
            transparent 2px,
            ${theme.colors.scanline} 2px,
            ${theme.colors.scanline} 4px
          )`,
          pointerEvents: 'none',
          zIndex: 10,
        }}
      />

      {/* Title bar */}
      <div
        style={{
          padding: '6px 14px',
          borderBottom: `1px solid ${theme.colors.border}`,
          fontFamily: theme.font.mono,
          fontSize: '11px',
          letterSpacing: '0.12em',
          textTransform: 'uppercase',
          color: theme.colors.primary,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
        }}
      >
        <span style={{
          width: '6px',
          height: '6px',
          borderRadius: '50%',
          background: theme.colors.primary,
          boxShadow: `0 0 6px ${theme.colors.primary}`,
        }} />
        {title}
      </div>

      {/* Content area */}
      <div style={{
        flex: 1,
        padding: noPadding ? 0 : '8px 12px',
        overflow: 'auto',
        minHeight: 0,
      }}>
        {children}
      </div>
    </div>
  );
}
