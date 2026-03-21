import React, { useEffect, useState, useRef, type ReactNode } from 'react';
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

// Unique ID for keyframes injection
let _styleInjected = false;
function injectBorderPulseStyle() {
  if (_styleInjected) return;
  _styleInjected = true;
  const style = document.createElement('style');
  style.textContent = `
    @keyframes glassBorderPulse {
      0%, 100% {
        border-color: rgba(58, 159, 232, 0.18);
        box-shadow: 0 0 15px rgba(58, 159, 232, 0.08), inset 0 0 30px rgba(5, 10, 20, 0.3);
      }
      50% {
        border-color: rgba(58, 159, 232, 0.35);
        box-shadow: 0 0 20px rgba(58, 159, 232, 0.15), inset 0 0 30px rgba(5, 10, 20, 0.3);
      }
    }
    @keyframes glassBorderFlash {
      0% {
        border-color: rgba(58, 159, 232, 0.6);
        box-shadow: 0 0 25px rgba(58, 159, 232, 0.3);
      }
      100% {
        border-color: rgba(58, 159, 232, 0.18);
        box-shadow: 0 0 15px rgba(58, 159, 232, 0.08);
      }
    }
  `;
  document.head.appendChild(style);
}

export const GlassPanel = React.memo(function GlassPanel({
  title,
  children,
  revealIndex = 0,
  bootComplete = true,
  style,
  className,
  noPadding = false,
}: GlassPanelProps) {
  const [revealed, setRevealed] = useState(false);
  const [flashDone, setFlashDone] = useState(false);
  const { play } = useSound();
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    injectBorderPulseStyle();
  }, []);

  useEffect(() => {
    if (!bootComplete) return;
    const timeout = setTimeout(() => {
      setRevealed(true);
      play('panelOpen');
      // Flash border on reveal, then transition to pulse
      setTimeout(() => setFlashDone(true), 400);
    }, revealIndex * theme.panelRevealDelay);
    return () => clearTimeout(timeout);
  }, [bootComplete, revealIndex, play]);

  const borderAnimation = !revealed
    ? 'none'
    : !flashDone
      ? 'glassBorderFlash 0.4s ease-out forwards'
      : `glassBorderPulse ${theme.animation.borderPulseDuration} ease-in-out infinite`;

  return (
    <div
      ref={panelRef}
      className={className}
      style={{
        background: theme.glassmorphism.background,
        backdropFilter: theme.glassmorphism.backdropFilter,
        WebkitBackdropFilter: theme.glassmorphism.backdropFilter,
        border: '1px solid rgba(58, 159, 232, 0.25)',
        boxShadow: '0 0 15px rgba(58, 159, 232, 0.08), inset 0 0 30px rgba(5, 10, 20, 0.3)',
        clipPath: theme.chamfer.clipPath,
        position: 'relative',
        overflow: 'hidden',
        opacity: revealed ? 1 : 0,
        transform: revealed ? 'translateY(0) scale(1)' : 'translateY(12px) scale(0.98)',
        transition: 'opacity 0.4s ease-out, transform 0.4s ease-out',
        display: 'flex',
        flexDirection: 'column',
        animation: borderAnimation,
        ...style,
      }}
    >
      {/* CRT scanline overlay -- subtler */}
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
          textShadow: `0 0 8px ${theme.colors.primaryDim}`,
        }}
      >
        <span style={{
          width: '6px',
          height: '6px',
          borderRadius: '50%',
          background: theme.colors.primary,
          boxShadow: `0 0 6px ${theme.colors.primary}, 0 0 12px ${theme.colors.primary}`,
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
});
