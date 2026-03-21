export const theme = {
  colors: {
    bg: '#050a14',           // deep space black
    bgPanel: 'rgba(8, 15, 30, 0.85)',
    bgPanelHover: 'rgba(12, 22, 42, 0.92)',
    primary: '#3A9FE8',      // electric blue
    primaryDim: 'rgba(58, 159, 232, 0.25)',
    accent: '#22c55e',       // green nominal
    warning: '#eab308',      // amber warning
    critical: '#ef4444',     // red critical
    text: '#e2e8f0',
    textDim: '#64748b',
    textMuted: '#475569',
    border: 'rgba(58, 159, 232, 0.18)',
    borderHover: 'rgba(58, 159, 232, 0.35)',
    scanline: 'rgba(58, 159, 232, 0.015)',  // subtler scanlines for glassmorphism
  },
  font: {
    mono: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace",
  },
  glassmorphism: {
    background: 'rgba(5, 10, 20, 0.18)',
    backdropFilter: 'blur(12px) saturate(1.3)',
    border: '1px solid rgba(58, 159, 232, 0.18)',
    boxShadow: '0 0 15px rgba(58, 159, 232, 0.08)',
  },
  chamfer: {
    // 45deg chamfer on top-right and bottom-left (panels)
    clipPath: 'polygon(0 0, calc(100% - 14px) 0, 100% 14px, 100% 100%, 14px 100%, 0 calc(100% - 14px))',
    // smaller chamfer for buttons
    buttonClipPath: 'polygon(0 0, calc(100% - 8px) 0, 100% 8px, 100% 100%, 8px 100%, 0 calc(100% - 8px))',
  },
  animation: {
    borderPulseDuration: '2s',
    radarSweepDuration: '4s',
    hoverGlowColor: 'rgba(58, 159, 232, 0.4)',
  },
  panelRevealDelay: 200, // ms between staggered panel reveals
} as const;

export type Theme = typeof theme;
