import { useCallback, useRef } from 'react';
import { useDashboard } from '../dashboard/DashboardContext';

type SoundName = 'boot' | 'click' | 'buttonPress' | 'hover' | 'nullClick';

const SOUND_MAP: Record<SoundName, { src: string; volume: number; category: 'system' | 'alert' | 'ui'; minMode: 'muted' | 'alerts' | 'full'; cooldownMs: number }> = {
  boot:        { src: '/soundfx/short_terminal_dashui_load.mp3', volume: 0.18, category: 'system', minMode: 'alerts', cooldownMs: 2400 },
  click:       { src: '/soundfx/clicking_on_any_object.mp3', volume: 0.07, category: 'ui', minMode: 'full', cooldownMs: 120 },
  buttonPress: { src: '/soundfx/button_press.mp3', volume: 0.11, category: 'alert', minMode: 'alerts', cooldownMs: 180 },
  hover:       { src: '/soundfx/cursor_hover.mp3', volume: 0.03, category: 'ui', minMode: 'full', cooldownMs: 320 },
  nullClick:   { src: '/soundfx/clicking_anywhere_else_that_does_nothing.mp3', volume: 0.04, category: 'alert', minMode: 'alerts', cooldownMs: 260 },
};

const audioCache = new Map<string, HTMLAudioElement>();
const soundLastPlayedAt = new Map<SoundName, number>();

function getAudio(src: string, volume: number): HTMLAudioElement {
  let audio = audioCache.get(src);
  if (!audio) {
    audio = new Audio(src);
    audioCache.set(src, audio);
  }
  audio.volume = volume;
  return audio;
}

export function useSound() {
  const hoverDebounce = useRef<number>(0);
  const { soundMode } = useDashboard();

  const play = useCallback((name: SoundName) => {
    const config = SOUND_MAP[name];
    if (!config) return;
    if (soundMode === 'muted') return;
    if (soundMode === 'alerts' && config.minMode === 'full') return;

    if (name === 'hover') {
      const now = Date.now();
      if (now - hoverDebounce.current < 320) return;
      hoverDebounce.current = now;
    }

    const now = Date.now();
    const lastPlayedAt = soundLastPlayedAt.get(name) ?? 0;
    if (now - lastPlayedAt < config.cooldownMs) return;
    soundLastPlayedAt.set(name, now);

    const audio = getAudio(config.src, config.volume);
    audio.currentTime = 0;
    audio.play().catch(() => {}); // ignore autoplay restrictions
  }, [soundMode]);

  return { play };
}
