import React, { useEffect, useRef, useCallback } from 'react';
import type { ConjunctionEvent } from '../types/api';
import { riskLevelForEvent, riskColor, pcProxy } from '../types/api';
import { theme } from '../styles/theme';
import type { ThreatSeverityFilter } from '../types/dashboard';
import {
  hasActiveThreatSeverityFilter,
  threatFilterAllowsRiskLevel,
} from '../types/dashboard';

/**
 * Conjunction Bullseye Plot with animated radar sweep.
 *
 * Radial distance = time-to-closest-approach (TCA). Center = now.
 * Angle = approach bearing derived from relative ECI positions.
 * Color: Watch > 5km, Warning 1-5km, Critical < 1km.
 */

interface Props {
  conjunctions: ConjunctionEvent[];
  selectedSatId: string | null;
  nowEpochS: number;   // current sim epoch in seconds
  maxTcaSeconds?: number;
  severityFilter?: ThreatSeverityFilter;
}

const DEFAULT_MAX_TCA_S = 5400;       // 90 minutes max on bullseye
const SWEEP_SPEED_DEG = 1.5;  // degrees per frame (~4s full rotation at 60fps)

function bullseyeRings(maxTcaSeconds: number): Array<{ s: number; label: string }> {
  if (maxTcaSeconds <= 5400) {
    return [
      { s: 900, label: '15m' },
      { s: 2700, label: '45m' },
      { s: 5400, label: '90m' },
    ];
  }
  if (maxTcaSeconds <= 21600) {
    return [
      { s: 3600, label: '1h' },
      { s: 10800, label: '3h' },
      { s: 21600, label: '6h' },
    ];
  }
  return [
    { s: 21600, label: '6h' },
    { s: 43200, label: '12h' },
    { s: 86400, label: '24h' },
  ];
}

function approachAngle(
  satPos: [number, number, number],
  debPos: [number, number, number],
): number {
  const dx = debPos[0] - satPos[0];
  const dy = debPos[1] - satPos[1];
  return Math.atan2(dy, dx);
}

export const ConjunctionBullseye = React.memo(function ConjunctionBullseye({ conjunctions, selectedSatId, nowEpochS, maxTcaSeconds = DEFAULT_MAX_TCA_S, severityFilter }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sweepAngleRef = useRef(0);

  // Store rapidly-changing data in refs so draw callback is stable
  const conjunctionsRef = useRef(conjunctions);
  const selectedSatIdRef = useRef(selectedSatId);
  const nowEpochSRef = useRef(nowEpochS);
  const maxTcaSecondsRef = useRef(maxTcaSeconds);
  const severityFilterRef = useRef<ThreatSeverityFilter | undefined>(severityFilter);
  conjunctionsRef.current = conjunctions;
  selectedSatIdRef.current = selectedSatId;
  nowEpochSRef.current = nowEpochS;
  maxTcaSecondsRef.current = maxTcaSeconds;
  severityFilterRef.current = severityFilter;

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const currentConjunctions = conjunctionsRef.current;
    const currentSelectedSatId = selectedSatIdRef.current;
    const currentNowEpochS = nowEpochSRef.current;
    const currentMaxTcaSeconds = Math.max(900, maxTcaSecondsRef.current);
    const currentSeverityFilter = severityFilterRef.current;

    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    const lw = w / dpr;
    const lh = h / dpr;
    const cx = lw / 2;
    const cy = lh / 2;
    const R = Math.min(cx, cy) - 24;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // Clear -- transparent bg (let GlassPanel show through)
    ctx.clearRect(0, 0, lw, lh);

    // Advance sweep angle
    sweepAngleRef.current = (sweepAngleRef.current + SWEEP_SPEED_DEG) % 360;
    const sweepRad = (sweepAngleRef.current * Math.PI) / 180;

    // filter to selected satellite if any
    const events = currentSelectedSatId
      ? currentConjunctions.filter(c => c.satellite_id === currentSelectedSatId)
      : currentConjunctions;
    const filteredEvents = currentSeverityFilter && hasActiveThreatSeverityFilter(currentSeverityFilter)
      ? events.filter(event => threatFilterAllowsRiskLevel(riskLevelForEvent(event), currentSeverityFilter))
      : currentSeverityFilter
        ? []
        : events;

    // Draw pulsing concentric rings
    const time = Date.now() / 1000;
    drawRings(ctx, cx, cy, R, time);

    // Radar sweep line with trail
    drawSweepLine(ctx, cx, cy, R, sweepRad);

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

    // center dot — draw BEFORE conjunction dots so events render on top
    ctx.save();
    ctx.fillStyle = theme.colors.primary;
    ctx.shadowColor = theme.colors.primary;
    ctx.shadowBlur = 8;
    ctx.beginPath();
    ctx.arc(cx, cy, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    if (events.length === 0) {
      // Empty state with pulsing text
      ctx.save();
      ctx.fillStyle = theme.colors.textMuted;
      ctx.font = `11px ${theme.font.mono}`;
      ctx.textAlign = 'center';
      const pulseAlpha = 0.4 + 0.6 * Math.abs(Math.sin(time * 1.5));
      ctx.globalAlpha = pulseAlpha;
      ctx.fillText('AWAITING CONJUNCTION DATA', cx, cy + 4);
      ctx.globalAlpha = 1;
      ctx.restore();
    } else if (filteredEvents.length === 0) {
      ctx.save();
      ctx.fillStyle = theme.colors.textMuted;
      ctx.font = `11px ${theme.font.mono}`;
      ctx.textAlign = 'center';
      const pulseAlpha = 0.4 + 0.6 * Math.abs(Math.sin(time * 1.5));
      ctx.globalAlpha = pulseAlpha;
      ctx.fillText('FILTERS HIDE ALL EVENTS', cx, cy + 4);
      ctx.globalAlpha = 1;
      ctx.restore();
    } else {
      // plot conjunction events
      let redCount = 0;
      let yellowCount = 0;
      let greenCount = 0;

       for (const evt of filteredEvents) {
        const dtca = evt.tca_epoch_s - currentNowEpochS;
        if (dtca < -300) continue; // skip events more than 5min in the past

        const tFrac = Math.min(1, Math.max(0, Math.abs(dtca) / currentMaxTcaSeconds));
        // Minimum 10px radial offset so events at/near TCA=now don't hide under center dot
        const MIN_RADIAL_PX = 10;
        const rr = Math.max(MIN_RADIAL_PX, tFrac * R);

        const angle = approachAngle(evt.sat_pos_eci_km, evt.deb_pos_eci_km);
        const px = cx + rr * Math.cos(angle);
        const py = cy + rr * Math.sin(angle);

        const risk = riskLevelForEvent(evt);
        const color = riskColor(risk);
        const pc = pcProxy(evt.miss_distance_km, evt.approach_speed_km_s);

        if (risk === 'red') redCount++;
        else if (risk === 'yellow') yellowCount++;
        else greenCount++;

        // Check if sweep just passed this dot
        const dotAngle = ((angle % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);
        const sweepNorm = ((sweepRad % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);
        const angleDiff = ((sweepNorm - dotAngle) + 2 * Math.PI) % (2 * Math.PI);
        const justSwept = angleDiff < 0.5 && angleDiff > 0; // ~30 degrees behind sweep

        // Glow pass — Pc-influenced opacity and size
        ctx.save();
        ctx.shadowColor = color;
        ctx.shadowBlur = justSwept ? 12 : 6;
        ctx.fillStyle = color;
        const baseAlpha = evt.collision ? 1.0 : justSwept ? 0.9 : 0.55 + 0.35 * pc;
        ctx.globalAlpha = baseAlpha;
        ctx.beginPath();
        const ptSize = risk === 'red' ? 3.5 : risk === 'yellow' ? 2.5 : 1.8;
        const pcBoost = pc > 0.1 ? 0.5 * pc : 0;
        ctx.arc(px, py, ptSize + pcBoost + (justSwept ? 1.5 : 0), 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();

        // collision marker -- extra ring
        if (evt.collision) {
          ctx.save();
          ctx.strokeStyle = color;
          ctx.shadowColor = color;
          ctx.shadowBlur = 8;
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.arc(px, py, ptSize + 3, 0, Math.PI * 2);
          ctx.stroke();
          ctx.restore();
        }
      }

      // threat summary
      ctx.save();
      ctx.font = `10px ${theme.font.mono}`;
      ctx.textAlign = 'left';
      let sy = lh - 8;
      if (greenCount > 0) {
        ctx.fillStyle = theme.colors.accent;
        ctx.fillText(`${greenCount} WATCH (>5km)`, 8, sy);
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
    }

    // selected sat label
    if (currentSelectedSatId) {
      ctx.save();
      ctx.font = `9px ${theme.font.mono}`;
      ctx.fillStyle = theme.colors.textDim;
      ctx.textAlign = 'center';
      ctx.fillText(currentSelectedSatId, cx, cy + R + 24);
      ctx.restore();
    }

    ctx.restore();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  function drawRings(ctx: CanvasRenderingContext2D, cx: number, cy: number, R: number, time: number) {
    // TCA time rings with pulsing opacity
    const currentMaxTcaSeconds = Math.max(900, maxTcaSecondsRef.current);
    const timeRings = bullseyeRings(currentMaxTcaSeconds);
    for (let i = 0; i < timeRings.length; i++) {
      const ring = timeRings[i];
      const rr = (ring.s / currentMaxTcaSeconds) * R;
      // Pulsing opacity with slight phase offset per ring
      const pulse = 0.06 + 0.09 * Math.abs(Math.sin(time * 1.2 + i * 0.8));
      ctx.strokeStyle = `rgba(255,255,255,${pulse})`;
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.arc(cx, cy, rr, 0, Math.PI * 2);
      ctx.stroke();

      ctx.fillStyle = 'rgba(255,255,255,0.2)';
      ctx.font = `8px ${theme.font.mono}`;
      ctx.textAlign = 'left';
      ctx.fillText(ring.label, cx + rr + 3, cy - 3);
    }

    // innermost danger zone fill (pulsing)
    const innerR = (timeRings[0].s / currentMaxTcaSeconds) * R;
    const dangerPulse = 0.06 + 0.04 * Math.abs(Math.sin(time * 2));
    ctx.fillStyle = `rgba(239,68,68,${dangerPulse})`;
    ctx.beginPath();
    ctx.arc(cx, cy, innerR, 0, Math.PI * 2);
    ctx.fill();
  }

  function drawSweepLine(ctx: CanvasRenderingContext2D, cx: number, cy: number, R: number, sweepRad: number) {
    // Sweep trail (fading ~30 deg arc)
    const trailDeg = 30;
    const steps = 20;
    for (let i = 0; i < steps; i++) {
      const frac = i / steps;
      const angle = sweepRad - (frac * trailDeg * Math.PI) / 180;
      const alpha = 0.12 * (1 - frac);
      ctx.strokeStyle = `rgba(58, 159, 232, ${alpha})`;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + R * Math.cos(angle), cy + R * Math.sin(angle));
      ctx.stroke();
    }

    // Main sweep line with gradient
    const grad = ctx.createLinearGradient(cx, cy, cx + R * Math.cos(sweepRad), cy + R * Math.sin(sweepRad));
    grad.addColorStop(0, 'rgba(58, 159, 232, 0)');
    grad.addColorStop(0.3, 'rgba(58, 159, 232, 0.2)');
    grad.addColorStop(1, 'rgba(58, 159, 232, 0.5)');
    ctx.strokeStyle = grad;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + R * Math.cos(sweepRad), cy + R * Math.sin(sweepRad));
    ctx.stroke();
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
      const dpr = window.devicePixelRatio || 1;
      canvas.width = canvas.offsetWidth * dpr;
      canvas.height = canvas.offsetHeight * dpr;
    });
    obs.observe(canvas);
    const dpr = window.devicePixelRatio || 1;
    canvas.width = canvas.offsetWidth * dpr;
    canvas.height = canvas.offsetHeight * dpr;
    return () => obs.disconnect();
  }, []);

  return (
    <canvas
      ref={canvasRef}
      style={{ flex: 1, width: '100%', height: '100%', minHeight: 0, display: 'block' }}
    />
  );
});
