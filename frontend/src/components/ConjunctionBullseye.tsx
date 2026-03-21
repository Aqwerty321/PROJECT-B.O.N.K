import { useEffect, useRef, useCallback } from 'react';
import type { ConjunctionEvent } from '../types/api';
import { riskLevelFromDistance, riskColor } from '../types/api';
import { theme } from '../styles/theme';

/**
 * Conjunction Bullseye Plot -- now driven by real /api/debug/conjunctions data.
 *
 * Radial distance = time-to-closest-approach (TCA). Center = now.
 * Angle = approach bearing derived from relative ECI positions.
 * Color: Green > 5km, Yellow 1-5km, Red < 1km.
 */

interface Props {
  conjunctions: ConjunctionEvent[];
  selectedSatId: string | null;
  nowEpochS: number;   // current sim epoch in seconds
}

const MAX_TCA_S = 5400;       // 90 minutes max on bullseye
const RISK_RINGS_KM = [1, 5]; // red / yellow boundaries

function approachAngle(
  satPos: [number, number, number],
  debPos: [number, number, number],
): number {
  const dx = debPos[0] - satPos[0];
  const dy = debPos[1] - satPos[1];
  return Math.atan2(dy, dx);
}

export function ConjunctionBullseye({ conjunctions, selectedSatId, nowEpochS }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    const cx = w / 2;
    const cy = h / 2;
    const R = Math.min(cx, cy) - 24;

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = theme.colors.bg;
    ctx.fillRect(0, 0, w, h);

    // filter to selected satellite if any
    const events = selectedSatId
      ? conjunctions.filter(c => c.satellite_id === selectedSatId)
      : conjunctions;

    if (events.length === 0 && !selectedSatId) {
      ctx.fillStyle = theme.colors.textMuted;
      ctx.font = `12px ${theme.font.mono}`;
      ctx.textAlign = 'center';
      ctx.fillText('No conjunctions', cx, cy - 6);
      ctx.fillText('detected', cx, cy + 12);
      drawRings(ctx, cx, cy, R);
      return;
    }

    drawRings(ctx, cx, cy, R);

    // crosshairs
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 0.5;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(cx - R - 8, cy); ctx.lineTo(cx + R + 8, cy);
    ctx.moveTo(cx, cy - R - 8); ctx.lineTo(cx, cy + R + 8);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();

    // compass labels
    ctx.save();
    ctx.fillStyle = 'rgba(255,255,255,0.25)';
    ctx.font = `9px ${theme.font.mono}`;
    ctx.textAlign = 'center';
    ctx.fillText('N', cx, cy - R - 4);
    ctx.fillText('S', cx, cy + R + 12);
    ctx.textAlign = 'left';
    ctx.fillText('E', cx + R + 4, cy + 4);
    ctx.textAlign = 'right';
    ctx.fillText('W', cx - R - 4, cy + 4);
    ctx.restore();

    // plot conjunction events
    let redCount = 0;
    let yellowCount = 0;
    let greenCount = 0;

    for (const evt of events) {
      const dtca = evt.tca_epoch_s - nowEpochS;
      if (dtca < -300) continue; // skip events more than 5min in the past

      const tFrac = Math.min(1, Math.max(0, Math.abs(dtca) / MAX_TCA_S));
      const rr = tFrac * R;

      const angle = approachAngle(evt.sat_pos_eci_km, evt.deb_pos_eci_km);
      const px = cx + rr * Math.cos(angle);
      const py = cy + rr * Math.sin(angle);

      const risk = riskLevelFromDistance(evt.miss_distance_km);
      const color = riskColor(risk);

      if (risk === 'red') redCount++;
      else if (risk === 'yellow') yellowCount++;
      else greenCount++;

      // draw point
      ctx.fillStyle = color;
      ctx.globalAlpha = evt.collision ? 1.0 : 0.8;
      ctx.beginPath();
      const ptSize = risk === 'red' ? 3.5 : risk === 'yellow' ? 2.5 : 1.8;
      ctx.arc(px, py, ptSize, 0, Math.PI * 2);
      ctx.fill();

      // collision marker -- extra ring
      if (evt.collision) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(px, py, ptSize + 3, 0, Math.PI * 2);
        ctx.stroke();
      }
    }
    ctx.globalAlpha = 1;

    // center dot
    ctx.save();
    ctx.fillStyle = theme.colors.primary;
    ctx.shadowColor = theme.colors.primary;
    ctx.shadowBlur = 8;
    ctx.beginPath();
    ctx.arc(cx, cy, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    // selected sat label
    if (selectedSatId) {
      ctx.save();
      ctx.font = `9px ${theme.font.mono}`;
      ctx.fillStyle = theme.colors.textDim;
      ctx.textAlign = 'center';
      ctx.fillText(selectedSatId, cx, cy + R + 24);
      ctx.restore();
    }

    // threat summary
    ctx.save();
    ctx.font = `10px ${theme.font.mono}`;
    ctx.textAlign = 'left';
    let sy = h - 8;
    if (greenCount > 0) {
      ctx.fillStyle = theme.colors.accent;
      ctx.fillText(`${greenCount} NOMINAL (>5km)`, 8, sy);
      sy -= 14;
    }
    if (yellowCount > 0) {
      ctx.fillStyle = theme.colors.warning;
      ctx.fillText(`${yellowCount} WARNING (1-5km)`, 8, sy);
      sy -= 14;
    }
    if (redCount > 0) {
      ctx.fillStyle = theme.colors.critical;
      ctx.fillText(`${redCount} CRITICAL (<1km)`, 8, sy);
    }
    ctx.restore();
  }, [conjunctions, selectedSatId, nowEpochS]);

  function drawRings(ctx: CanvasRenderingContext2D, cx: number, cy: number, R: number) {
    // TCA time rings
    const timeRings = [
      { s: 900,  label: '15m' },
      { s: 2700, label: '45m' },
      { s: 5400, label: '90m' },
    ];
    for (const ring of timeRings) {
      const rr = (ring.s / MAX_TCA_S) * R;
      ctx.strokeStyle = 'rgba(255,255,255,0.08)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.arc(cx, cy, rr, 0, Math.PI * 2);
      ctx.stroke();

      ctx.fillStyle = 'rgba(255,255,255,0.2)';
      ctx.font = `8px ${theme.font.mono}`;
      ctx.textAlign = 'left';
      ctx.fillText(ring.label, cx + rr + 3, cy - 3);
    }

    // innermost danger zone fill
    const innerR = (900 / MAX_TCA_S) * R;
    ctx.fillStyle = 'rgba(239,68,68,0.08)';
    ctx.beginPath();
    ctx.arc(cx, cy, innerR, 0, Math.PI * 2);
    ctx.fill();
  }

  useEffect(() => {
    let running = true;
    const loop = () => {
      if (!running) return;
      draw();
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
    return () => { running = false; };
  }, [draw]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const obs = new ResizeObserver(() => {
      canvas.width = canvas.offsetWidth * (window.devicePixelRatio || 1);
      canvas.height = canvas.offsetHeight * (window.devicePixelRatio || 1);
      const ctx = canvas.getContext('2d');
      if (ctx) ctx.scale(window.devicePixelRatio || 1, window.devicePixelRatio || 1);
    });
    obs.observe(canvas);
    canvas.width = canvas.offsetWidth * (window.devicePixelRatio || 1);
    canvas.height = canvas.offsetHeight * (window.devicePixelRatio || 1);
    const ctx = canvas.getContext('2d');
    if (ctx) ctx.scale(window.devicePixelRatio || 1, window.devicePixelRatio || 1);
    return () => obs.disconnect();
  }, []);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: '100%', height: '100%', display: 'block' }}
    />
  );
}
