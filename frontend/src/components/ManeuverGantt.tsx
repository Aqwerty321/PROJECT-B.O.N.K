import { useEffect, useRef, useCallback, memo } from 'react';
import type { BurnsResponse, ExecutedBurn, PendingBurn } from '../types/api';
import { theme } from '../styles/theme';

interface ManeuverGanttProps {
  burns: BurnsResponse | null;
  selectedSatId: string | null;
  nowEpochS: number;        // current sim epoch in seconds for sweep line
}

// ---- constants ----
const ROW_HEIGHT = 22;
const HEADER_HEIGHT = 24;
const LABEL_WIDTH = 90;
const MIN_BLOCK_WIDTH = 4;
const BURN_DURATION_S = 120; // assume 2min burn for display

function burnColor(burn: ExecutedBurn | PendingBurn, isPending: boolean): string {
  if (isPending) return theme.colors.primary;
  const b = burn as ExecutedBurn;
  if (b.graveyard_burn) return '#6b7280';
  if (b.recovery_burn) return theme.colors.warning;
  return theme.colors.accent;
}

export default memo(function ManeuverGantt({ burns, selectedSatId, nowEpochS }: ManeuverGanttProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const drawRef = useRef<() => void>(() => {});

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    const dpr = window.devicePixelRatio || 1;
    const lw = w / dpr;
    const lh = h / dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // Clear -- transparent bg
    ctx.clearRect(0, 0, lw, lh);

    // Always draw grid background
    const chartWidth = lw - LABEL_WIDTH - 10;

    if (!burns || (burns.executed.length === 0 && burns.pending.length === 0)) {
      // Draw basic grid even with no data
      drawEmptyGrid(ctx, lw, lh, chartWidth);

      // Empty state text (CSS animation handles pulsing)
      ctx.save();
      ctx.fillStyle = theme.colors.textMuted;
      ctx.font = `11px ${theme.font.mono}`;
      ctx.textAlign = 'center';
      ctx.globalAlpha = 0.7;
      ctx.fillText('AWAITING BURN DATA', lw / 2, lh / 2);
      ctx.globalAlpha = 1;
      ctx.restore();
      ctx.restore();
      return;
    }

    // Collect all burns, filter by selected satellite if any
    const allExecuted = selectedSatId
      ? burns.executed.filter(b => b.satellite_id === selectedSatId)
      : burns.executed;
    const allPending = selectedSatId
      ? burns.pending.filter(b => b.satellite_id === selectedSatId)
      : burns.pending;

    // Group by satellite
    const satIds = new Set<string>();
    allExecuted.forEach(b => satIds.add(b.satellite_id));
    allPending.forEach(b => satIds.add(b.satellite_id));
    const satellites = Array.from(satIds).sort();

    if (satellites.length === 0) {
      drawEmptyGrid(ctx, lw, lh, chartWidth);
      ctx.restore();
      return;
    }

    // Compute time range
    const allEpochs: number[] = [];
    const parseEpoch = (iso: string) => new Date(iso).getTime() / 1000;
    allExecuted.forEach(b => allEpochs.push(parseEpoch(b.burn_epoch)));
    allPending.forEach(b => allEpochs.push(parseEpoch(b.burn_epoch)));

    if (allEpochs.length === 0) { ctx.restore(); return; }

    const tMin = Math.min(...allEpochs) - 600;
    const tMax = Math.max(...allEpochs) + 600;
    const tRange = Math.max(tMax - tMin, 3600); // at least 1 hour range

    const timeToX = (t: number) => LABEL_WIDTH + ((t - tMin) / tRange) * chartWidth;

    // Header -- time axis
    ctx.fillStyle = theme.colors.textMuted;
    ctx.font = `9px ${theme.font.mono}`;
    ctx.textAlign = 'center';

    const tickCount = Math.min(8, Math.floor(chartWidth / 80));
    for (let i = 0; i <= tickCount; i++) {
      const t = tMin + (i / tickCount) * tRange;
      const x = timeToX(t);
      const date = new Date(t * 1000);
      const label = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
      ctx.fillText(label, x, HEADER_HEIGHT - 4);

      // tick line
      ctx.strokeStyle = 'rgba(255,255,255,0.06)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(x, HEADER_HEIGHT);
      ctx.lineTo(x, lh);
      ctx.stroke();
    }

    // Rows
    for (let i = 0; i < satellites.length; i++) {
      const satId = satellites[i];
      const y = HEADER_HEIGHT + i * ROW_HEIGHT;

      // alternate row bg
      if (i % 2 === 0) {
        ctx.fillStyle = 'rgba(255,255,255,0.02)';
        ctx.fillRect(0, y, lw, ROW_HEIGHT);
      }

      // row divider
      ctx.strokeStyle = 'rgba(255,255,255,0.04)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(LABEL_WIDTH, y + ROW_HEIGHT);
      ctx.lineTo(lw, y + ROW_HEIGHT);
      ctx.stroke();

      // satellite label
      ctx.fillStyle = satId === selectedSatId ? theme.colors.primary : theme.colors.textDim;
      ctx.font = `10px ${theme.font.mono}`;
      ctx.textAlign = 'right';
      const shortId = satId.length > 10 ? satId.slice(0, 10) : satId;
      ctx.fillText(shortId, LABEL_WIDTH - 6, y + ROW_HEIGHT / 2 + 3);

      // draw executed burns with neon glow
      const satExecuted = allExecuted.filter(b => b.satellite_id === satId);
      for (const burn of satExecuted) {
        const t = parseEpoch(burn.burn_epoch);
        const x1 = timeToX(t);
        const x2 = Math.max(x1 + MIN_BLOCK_WIDTH, timeToX(t + BURN_DURATION_S));
        const bw = x2 - x1;
        const color = burnColor(burn, false);

        // Outer glow pass
        ctx.save();
        ctx.shadowColor = color;
        ctx.shadowBlur = 6;
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.3;
        ctx.fillRect(x1 - 1, y + 2, bw + 2, ROW_HEIGHT - 4);
        ctx.restore();

        // Inner fill
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.85;
        ctx.fillRect(x1, y + 3, bw, ROW_HEIGHT - 6);
        ctx.globalAlpha = 1;

        // border
        ctx.strokeStyle = color;
        ctx.lineWidth = 0.8;
        ctx.strokeRect(x1, y + 3, bw, ROW_HEIGHT - 6);
      }

      // draw pending burns (dashed outline + glow)
      const satPending = allPending.filter(b => b.satellite_id === satId);
      for (const burn of satPending) {
        const t = parseEpoch(burn.burn_epoch);
        const x1 = timeToX(t);
        const x2 = Math.max(x1 + MIN_BLOCK_WIDTH, timeToX(t + BURN_DURATION_S));
        const bw = x2 - x1;
        const color = burnColor(burn, true);

        ctx.setLineDash([3, 3]);
        ctx.save();
        ctx.shadowColor = color;
        ctx.shadowBlur = 4;
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.2;
        ctx.strokeRect(x1, y + 3, bw, ROW_HEIGHT - 6);
        ctx.restore();
        ctx.setLineDash([]);

        ctx.fillStyle = color;
        ctx.globalAlpha = 0.2;
        ctx.fillRect(x1, y + 3, bw, ROW_HEIGHT - 6);
        ctx.globalAlpha = 1;
      }
    }

    // Sweep line at current sim time
    const sweepX = timeToX(nowEpochS);
    if (sweepX >= LABEL_WIDTH && sweepX <= lw - 10) {
      // Wide glow
      ctx.save();
      ctx.strokeStyle = 'rgba(58, 159, 232, 0.15)';
      ctx.lineWidth = 6;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, lh - 20);
      ctx.stroke();
      // Medium
      ctx.strokeStyle = 'rgba(58, 159, 232, 0.3)';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, lh - 20);
      ctx.stroke();
      // Sharp
      ctx.strokeStyle = theme.colors.primary;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, lh - 20);
      ctx.stroke();
      ctx.restore();
    }

    // Legend
    const legendY = lh - 14;
    const legendItems = [
      { label: 'Avoidance', color: theme.colors.accent },
      { label: 'Recovery', color: theme.colors.warning },
      { label: 'Graveyard', color: '#6b7280' },
      { label: 'Pending', color: theme.colors.primary },
    ];
    let lx = LABEL_WIDTH;
    ctx.font = `9px ${theme.font.mono}`;
    ctx.textAlign = 'left';
    for (const item of legendItems) {
      ctx.fillStyle = item.color;
      ctx.fillRect(lx, legendY - 6, 10, 8);
      ctx.fillStyle = theme.colors.textMuted;
      ctx.fillText(item.label, lx + 14, legendY + 1);
      lx += ctx.measureText(item.label).width + 28;
    }

    ctx.restore();
  }, [burns, selectedSatId, nowEpochS]);

  function drawEmptyGrid(ctx: CanvasRenderingContext2D, lw: number, lh: number, chartWidth: number) {
    // Draw basic time axis grid even with no data
    ctx.fillStyle = theme.colors.textMuted;
    ctx.font = `9px ${theme.font.mono}`;
    ctx.textAlign = 'center';
    const tickCount = Math.min(8, Math.floor(chartWidth / 80));
    for (let i = 0; i <= tickCount; i++) {
      const x = LABEL_WIDTH + (i / tickCount) * chartWidth;
      ctx.strokeStyle = 'rgba(255,255,255,0.04)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(x, HEADER_HEIGHT);
      ctx.lineTo(x, lh);
      ctx.stroke();
    }
    // horizontal row lines
    for (let y = HEADER_HEIGHT; y < lh; y += ROW_HEIGHT) {
      ctx.strokeStyle = 'rgba(255,255,255,0.03)';
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(LABEL_WIDTH, y);
      ctx.lineTo(lw, y);
      ctx.stroke();
    }
  }

  // Keep drawRef pointing at the latest draw function
  drawRef.current = draw;

  // Redraw when data changes (no RAF loop needed -- data updates at ~1Hz)
  useEffect(() => {
    draw();
  }, [draw]);

  // Resize observer — uses drawRef so the observer never needs re-subscribing
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const obs = new ResizeObserver(() => {
      const dpr = window.devicePixelRatio || 1;
      canvas.width = canvas.offsetWidth * dpr;
      canvas.height = canvas.offsetHeight * dpr;
      drawRef.current();
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
      style={{ width: '100%', height: '100%', display: 'block' }}
    />
  );
});
