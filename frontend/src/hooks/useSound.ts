import { useCallback, useRef } from 'react';

type SoundName = 'boot' | 'panelOpen' | 'click' | 'buttonPress' | 'hover' | 'nullClick';

const SOUND_MAP: Record<SoundName, { src: string; volume: number }> = {
  boot:        { src: '/soundfx/short_terminal_dashui_load.mp3', volume: 0.5 },
  panelOpen:   { src: '/soundfx/short_UI_container_opening.mp3', volume: 0.3 },
  click:       { src: '/soundfx/clicking_on_any_object.mp3', volume: 0.2 },
  buttonPress: { src: '/soundfx/button_press.mp3', volume: 0.25 },
  hover:       { src: '/soundfx/cursor_hover.mp3', volume: 0.15 },
  nullClick:   { src: '/soundfx/clicking_anywhere_else_that_does_nothing.mp3', volume: 0.1 },
};

const audioCache = new Map<string, HTMLAudioElement>();

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

  const play = useCallback((name: SoundName) => {
    const config = SOUND_MAP[name];
    if (!config) return;

    if (name === 'hover') {
      const now = Date.now();
      if (now - hoverDebounce.current < 80) return;
      hoverDebounce.current = now;
    }

    const audio = getAudio(config.src, config.volume);
    audio.currentTime = 0;
    audio.play().catch(() => {}); // ignore autoplay restrictions
  }, []);

  return { play };
}
