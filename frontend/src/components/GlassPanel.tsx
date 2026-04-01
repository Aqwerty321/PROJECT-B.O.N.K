import React, { useEffect, useState, useRef, type ReactNode } from 'react';
import { theme } from '../styles/theme';

interface GlassPanelProps {
  title: string;
  children: ReactNode;
  revealIndex?: number;      // stagger order (0-based)
  bootComplete?: boolean;    // only reveal after boot
  style?: React.CSSProperties;
  className?: string;
  noPadding?: boolean;
  priority?: 'primary' | 'secondary' | 'support';
  accentColor?: string;
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
  priority = 'secondary',
  accentColor = theme.colors.primary,
}: GlassPanelProps) {
  const [revealed, setRevealed] = useState(false);
  const [flashDone, setFlashDone] = useState(false);
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    injectBorderPulseStyle();
  }, []);

  useEffect(() => {
    if (!bootComplete) return;
    const timeout = setTimeout(() => {
      setRevealed(true);
      // Flash border on reveal, then transition to pulse
      setTimeout(() => setFlashDone(true), 400);
    }, revealIndex * theme.panelRevealDelay);
    return () => clearTimeout(timeout);
  }, [bootComplete, revealIndex]);

  const borderAnimation = !revealed
    ? 'none'
    : !flashDone
      ? 'glassBorderFlash 0.4s ease-out forwards'
      : `glassBorderPulse ${theme.animation.borderPulseDuration} ease-in-out infinite`;

  const isPrimary = priority === 'primary';
  const isSupport = priority === 'support';
  const panelBackground = isPrimary
    ? 'linear-gradient(180deg, rgba(7, 14, 28, 0.80), rgba(3, 8, 16, 0.78))'
    : isSupport
      ? 'linear-gradient(180deg, rgba(4, 10, 20, 0.68), rgba(2, 6, 14, 0.74))'
      : theme.glassmorphism.background;
  const panelBorder = isPrimary ? '1px solid rgba(58, 159, 232, 0.34)' : '1px solid rgba(58, 159, 232, 0.22)';
  const panelShadow = isPrimary
    ? '0 0 24px rgba(58, 159, 232, 0.10), inset 0 0 36px rgba(5, 10, 20, 0.34)'
    : isSupport
      ? '0 0 10px rgba(58, 159, 232, 0.05), inset 0 0 22px rgba(5, 10, 20, 0.25)'
      : '0 0 15px rgba(58, 159, 232, 0.08), inset 0 0 30px rgba(5, 10, 20, 0.3)';
  const titleBackground = isPrimary
    ? `linear-gradient(90deg, ${accentColor}18, rgba(58, 159, 232, 0.05) 45%, transparent 100%)`
    : isSupport
      ? 'linear-gradient(90deg, rgba(58, 159, 232, 0.05), transparent 40%)'
      : 'linear-gradient(90deg, rgba(58, 159, 232, 0.08), transparent 36%)';
  const titleColor = isPrimary ? theme.colors.text : accentColor;

  return (
    <div
      ref={panelRef}
      className={className}
      style={{
        background: panelBackground,
        backdropFilter: theme.glassmorphism.backdropFilter,
        WebkitBackdropFilter: theme.glassmorphism.backdropFilter,
        border: panelBorder,
        boxShadow: panelShadow,
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
      <div
        aria-hidden="true"
        style={{
          position: 'absolute',
          top: 0,
          left: '18px',
          right: '18px',
          height: isPrimary ? '2px' : '1px',
          background: `linear-gradient(90deg, transparent 0%, ${accentColor} 18%, ${accentColor} 82%, transparent 100%)`,
          opacity: isPrimary ? 0.75 : 0.45,
          zIndex: 2,
        }}
      />

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
          background: titleBackground,
          fontFamily: theme.font.mono,
          fontSize: '11px',
          letterSpacing: '0.12em',
          textTransform: 'uppercase',
          color: titleColor,
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
          background: accentColor,
          boxShadow: `0 0 6px ${accentColor}, 0 0 12px ${accentColor}`,
        }} />
        {title}
      </div>

      {/* Content area */}
      <div style={{
        flex: 1,
        padding: noPadding ? 0 : '8px 12px',
        overflow: 'auto',
        minHeight: 0,
        minWidth: 0,
        display: 'flex',
        flexDirection: 'column',
      }}>
        {children}
      </div>
    </div>
  );
});
