export const theme = {
  colors: {
    bg: '#060607',
    bgPanel: 'rgba(12, 13, 16, 0.92)',
    bgPanelHover: 'rgba(18, 20, 24, 0.96)',
    primary: '#58b8ff',
    primaryDim: 'rgba(88, 184, 255, 0.28)',
    accent: '#39d98a',
    warning: '#ffc247',
    critical: '#ff6262',
    text: '#f3f6fb',
    textDim: '#99a9bc',
    textMuted: '#68778b',
    border: 'rgba(88, 184, 255, 0.22)',
    borderHover: 'rgba(88, 184, 255, 0.42)',
    scanline: 'rgba(255, 255, 255, 0.012)',
  },
  font: {
    mono: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace",
  },
  glassmorphism: {
    background: 'rgba(10, 11, 13, 0.76)',
    backdropFilter: 'blur(10px) saturate(1.08)',
    border: '1px solid rgba(88, 184, 255, 0.20)',
    boxShadow: '0 0 20px rgba(88, 184, 255, 0.08)',
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
