import { useEffect, useRef, useCallback, memo } from 'react';
import type { BurnsResponse, ExecutedBurn, PendingBurn } from '../types/api';
import { theme } from '../styles/theme';

interface ManeuverGanttProps {
  burns: BurnsResponse | null;
  selectedSatId: string | null;
  nowEpochS: number;
}

const HEADER_HEIGHT = 34;
const FOOTER_HEIGHT = 34;
const LABEL_WIDTH = 110;
const MIN_ROW_HEIGHT = 6;
const MAX_ROW_HEIGHT = 36;
const MIN_BLOCK_WIDTH = 8;
const BURN_DURATION_S = 120;
const COOLDOWN_DURATION_S = 600;

function compactSatLabel(id: string): string {
  if (id.length <= 12) return id;
  const parts = id.split('-');
  if (parts.length >= 2) {
    return `${parts[0]}-${parts[parts.length - 1]}`;
  }
  return `${id.slice(0, 7)}...${id.slice(-3)}`;
}

function burnColor(burn: ExecutedBurn | PendingBurn, isPending: boolean): string {
  if (burn.graveyard_burn) return '#8892a0';
  if (burn.recovery_burn) return theme.colors.warning;
  return isPending ? theme.colors.primary : theme.colors.accent;
}

function drawPanelChrome(ctx: CanvasRenderingContext2D, width: number, height: number) {
  const plotBottom = height - FOOTER_HEIGHT;

  ctx.fillStyle = 'rgba(6, 9, 14, 0.94)';
  ctx.fillRect(0, 0, width, HEADER_HEIGHT);

  ctx.fillStyle = 'rgba(8, 11, 16, 0.72)';
  ctx.fillRect(0, HEADER_HEIGHT, LABEL_WIDTH, plotBottom - HEADER_HEIGHT);

  ctx.fillStyle = 'rgba(255, 255, 255, 0.015)';
  ctx.fillRect(LABEL_WIDTH, HEADER_HEIGHT, width - LABEL_WIDTH, plotBottom - HEADER_HEIGHT);

  ctx.strokeStyle = 'rgba(88, 184, 255, 0.14)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(LABEL_WIDTH + 0.5, HEADER_HEIGHT);
  ctx.lineTo(LABEL_WIDTH + 0.5, plotBottom);
  ctx.stroke();

  ctx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
  ctx.beginPath();
  ctx.moveTo(0, HEADER_HEIGHT + 0.5);
  ctx.lineTo(width, HEADER_HEIGHT + 0.5);
  ctx.stroke();

  ctx.beginPath();
  ctx.moveTo(0, plotBottom + 0.5);
  ctx.lineTo(width, plotBottom + 0.5);
  ctx.stroke();

  ctx.fillStyle = 'rgba(153, 169, 188, 0.62)';
  ctx.font = `10px ${theme.font.mono}`;
  ctx.textAlign = 'left';
  ctx.fillText('VEHICLE', 12, HEADER_HEIGHT - 10);
  ctx.fillText('UTC', LABEL_WIDTH + 12, HEADER_HEIGHT - 10);

  return plotBottom;
}

function drawCooldownWindow(
  ctx: CanvasRenderingContext2D,
  xStart: number,
  y: number,
  width: number,
  rowHeight: number,
) {
  const rectHeight = Math.max(4, rowHeight - 8);

  ctx.save();
  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = 'rgba(88, 184, 255, 0.78)';
  ctx.lineWidth = 1;
  ctx.strokeRect(xStart, y + 4, Math.max(width, 8), rectHeight);
  ctx.setLineDash([]);
  ctx.fillStyle = 'rgba(88, 184, 255, 0.14)';
  ctx.fillRect(xStart, y + 4, Math.max(width, 8), rectHeight);
  ctx.restore();
}

function drawBurnMarkers(
  ctx: CanvasRenderingContext2D,
  x1: number,
  x2: number,
  y: number,
  rowHeight: number,
  color: string,
  pending: boolean,
) {
  const top = y + 2;
  const bottom = y + rowHeight - 2;
  const radius = Math.max(1.6, Math.min(3.2, rowHeight * 0.12));

  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.2;
  ctx.globalAlpha = pending ? 0.92 : 1;
  ctx.lineCap = 'round';

  ctx.beginPath();
  ctx.moveTo(x1, top);
  ctx.lineTo(x1, bottom);
  ctx.stroke();

  ctx.beginPath();
  ctx.moveTo(x2, top);
  ctx.lineTo(x2, bottom);
  ctx.stroke();

  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(x1, top + 2, radius, 0, Math.PI * 2);
  ctx.fill();

  ctx.beginPath();
  ctx.arc(x2, top + 2, radius, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawEmptyGrid(ctx: CanvasRenderingContext2D, width: number, height: number, chartWidth: number) {
  const plotBottom = drawPanelChrome(ctx, width, height);
  const tickCount = Math.max(4, Math.min(8, Math.floor(chartWidth / 140)));

  ctx.fillStyle = 'rgba(153, 169, 188, 0.78)';
  ctx.font = `10px ${theme.font.mono}`;
  ctx.textAlign = 'center';
  for (let i = 0; i <= tickCount; i++) {
    const x = LABEL_WIDTH + (i / tickCount) * chartWidth;
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 0.6;
    ctx.beginPath();
    ctx.moveTo(x, HEADER_HEIGHT);
    ctx.lineTo(x, plotBottom);
    ctx.stroke();
  }

  for (let y = HEADER_HEIGHT; y < plotBottom; y += 28) {
    ctx.strokeStyle = 'rgba(255,255,255,0.035)';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
}

function drawEmptyState(
  ctx: CanvasRenderingContext2D,
  chartWidth: number,
  selectedSatId: string | null,
) {
  const top = HEADER_HEIGHT + 26;
  const rowY = top + 28;
  const baseX = LABEL_WIDTH + Math.max(24, chartWidth * 0.22);
  const burnWidth = Math.max(32, chartWidth * 0.12);
  const cooldownWidth = Math.max(52, (COOLDOWN_DURATION_S / 3600) * chartWidth * 0.4);

  ctx.save();
  ctx.fillStyle = 'rgba(226, 232, 240, 0.22)';
  ctx.font = `12px ${theme.font.mono}`;
  ctx.textAlign = 'left';
  ctx.fillText(selectedSatId ? `${selectedSatId} NO BURNS QUEUED` : 'FLEET BURN CLOCK IDLE', LABEL_WIDTH + 18, top);

  ctx.fillStyle = 'rgba(148, 163, 184, 0.62)';
  ctx.font = `10px ${theme.font.mono}`;
  ctx.fillText('This lane populates when evasion, recovery, or graveyard burns enter the command queue.', LABEL_WIDTH + 18, top + 18);

  ctx.fillStyle = 'rgba(57, 217, 138, 0.28)';
  ctx.fillRect(baseX, rowY, burnWidth, 20);
  ctx.strokeStyle = 'rgba(57, 217, 138, 0.78)';
  ctx.strokeRect(baseX, rowY, burnWidth, 20);
  ctx.fillStyle = 'rgba(226, 232, 240, 0.45)';
  ctx.fillText('Burn Window', baseX, rowY - 8);

  ctx.setLineDash([4, 3]);
  ctx.strokeStyle = 'rgba(88, 184, 255, 0.78)';
  ctx.strokeRect(baseX + burnWidth + 18, rowY, cooldownWidth, 20);
  ctx.setLineDash([]);
  ctx.fillText('Cooldown Hold', baseX + burnWidth + 18, rowY - 8);

  ctx.strokeStyle = 'rgba(255, 98, 98, 0.75)';
  ctx.setLineDash([2, 3]);
  ctx.beginPath();
  ctx.moveTo(baseX + burnWidth + cooldownWidth + 50, rowY - 6);
  ctx.lineTo(baseX + burnWidth + cooldownWidth + 50, rowY + 28);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.fillText('Conflict / blackout marker', baseX + burnWidth + cooldownWidth + 58, rowY + 8);
  ctx.restore();
}

export default memo(function ManeuverGantt({ burns, selectedSatId, nowEpochS }: ManeuverGanttProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const drawRef = useRef<() => void>(() => {});

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const lw = canvas.width / dpr;
    const lh = canvas.height / dpr;
    const chartWidth = lw - LABEL_WIDTH - 12;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, lw, lh);

    if (!burns || (burns.executed.length === 0 && burns.pending.length === 0)) {
      drawEmptyGrid(ctx, lw, lh, chartWidth);
      drawEmptyState(ctx, chartWidth, selectedSatId);
      ctx.restore();
      return;
    }

    const allExecuted = selectedSatId
      ? burns.executed.filter(burn => burn.satellite_id === selectedSatId)
      : burns.executed;
    const allPending = selectedSatId
      ? burns.pending.filter(burn => burn.satellite_id === selectedSatId)
      : burns.pending;

    const satIds = new Set<string>();
    allExecuted.forEach(burn => satIds.add(burn.satellite_id));
    allPending.forEach(burn => satIds.add(burn.satellite_id));
    const satellites = Array.from(satIds).sort();

    if (satellites.length === 0) {
      drawEmptyGrid(ctx, lw, lh, chartWidth);
      drawEmptyState(ctx, chartWidth, selectedSatId);
      ctx.restore();
      return;
    }

    const parseEpoch = (iso: string) => new Date(iso).getTime() / 1000;
    const allEpochs = [
      ...allExecuted.map(burn => parseEpoch(burn.burn_epoch)),
      ...allPending.map(burn => parseEpoch(burn.burn_epoch)),
    ];

    if (allEpochs.length === 0) {
      drawEmptyGrid(ctx, lw, lh, chartWidth);
      drawEmptyState(ctx, chartWidth, selectedSatId);
      ctx.restore();
      return;
    }

    const plotBottom = drawPanelChrome(ctx, lw, lh);
    const tMin = Math.min(...allEpochs) - 600;
    const tMax = Math.max(...allEpochs) + 600;
    const tRange = Math.max(tMax - tMin, 3600);
    const timeToX = (t: number) => LABEL_WIDTH + ((t - tMin) / tRange) * chartWidth;

    const rowAreaHeight = Math.max(72, plotBottom - HEADER_HEIGHT - 8);
    const rowHeight = Math.max(
      MIN_ROW_HEIGHT,
      Math.min(MAX_ROW_HEIGHT, Math.floor(rowAreaHeight / Math.max(satellites.length, 1))),
    );
    const rowsHeight = rowHeight * satellites.length;
    const rowsTop = HEADER_HEIGHT + Math.max(0, Math.floor((rowAreaHeight - rowsHeight) / 2));
    const labelFontSize = Math.max(7, Math.min(11, rowHeight * 0.7));

    ctx.fillStyle = 'rgba(153, 169, 188, 0.78)';
    ctx.font = `10px ${theme.font.mono}`;
    ctx.textAlign = 'center';

    const tickCount = Math.max(4, Math.min(8, Math.floor(chartWidth / 140)));
    for (let i = 0; i <= tickCount; i++) {
      const t = tMin + (i / tickCount) * tRange;
      const x = timeToX(t);
      const date = new Date(t * 1000);
      const label = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
      ctx.fillText(label, x, HEADER_HEIGHT - 10);

      ctx.strokeStyle = i === tickCount ? 'rgba(88, 184, 255, 0.10)' : 'rgba(255,255,255,0.06)';
      ctx.lineWidth = i === tickCount ? 1 : 0.6;
      ctx.beginPath();
      ctx.moveTo(x, HEADER_HEIGHT);
      ctx.lineTo(x, plotBottom);
      ctx.stroke();
    }

    for (let i = 0; i < satellites.length; i++) {
      const satId = satellites[i];
      const y = rowsTop + i * rowHeight;

      if (i % 2 === 0) {
        ctx.fillStyle = 'rgba(255,255,255,0.025)';
        ctx.fillRect(0, y, lw, rowHeight);
      }
      if (satId === selectedSatId) {
        ctx.fillStyle = 'rgba(88, 184, 255, 0.08)';
        ctx.fillRect(0, y, lw, rowHeight);
      }

      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.lineWidth = 0.6;
      ctx.beginPath();
      ctx.moveTo(0, y + rowHeight);
      ctx.lineTo(lw, y + rowHeight);
      ctx.stroke();

      ctx.fillStyle = satId === selectedSatId ? theme.colors.primary : theme.colors.textDim;
      ctx.font = `${labelFontSize}px ${theme.font.mono}`;
      ctx.textAlign = 'right';
      ctx.fillText(compactSatLabel(satId), LABEL_WIDTH - 8, y + rowHeight / 2 + Math.max(3, labelFontSize * 0.35));

      const satExecuted = allExecuted.filter(burn => burn.satellite_id === satId);
      for (const burn of satExecuted) {
        const t = parseEpoch(burn.burn_epoch);
        const x1 = timeToX(t);
        const x2 = Math.max(x1 + MIN_BLOCK_WIDTH, timeToX(t + BURN_DURATION_S));
        const bw = x2 - x1;
        const cooldownEnd = timeToX(t + BURN_DURATION_S + COOLDOWN_DURATION_S);
        const color = burnColor(burn, false);

        drawCooldownWindow(ctx, x2, y, cooldownEnd - x2, rowHeight);

        ctx.save();
        ctx.shadowColor = color;
        ctx.shadowBlur = 8;
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.34;
        ctx.fillRect(x1 - 1, y + 3, bw + 2, Math.max(4, rowHeight - 6));
        ctx.restore();

        ctx.fillStyle = color;
        ctx.globalAlpha = 0.9;
        ctx.fillRect(x1, y + 4, bw, Math.max(4, rowHeight - 8));
        ctx.globalAlpha = 1;

        ctx.strokeStyle = color;
        ctx.lineWidth = 1;
        ctx.strokeRect(x1, y + 4, bw, Math.max(4, rowHeight - 8));

        drawBurnMarkers(ctx, x1, x2, y, rowHeight, color, false);
      }

      const satPending = allPending.filter(burn => burn.satellite_id === satId);
      for (const burn of satPending) {
        const t = parseEpoch(burn.burn_epoch);
        const x1 = timeToX(t);
        const x2 = Math.max(x1 + MIN_BLOCK_WIDTH, timeToX(t + BURN_DURATION_S));
        const bw = x2 - x1;
        const cooldownEnd = timeToX(t + BURN_DURATION_S + COOLDOWN_DURATION_S);
        const color = burnColor(burn, true);

        drawCooldownWindow(ctx, x2, y, cooldownEnd - x2, rowHeight);

        ctx.setLineDash([3, 3]);
        ctx.save();
        ctx.shadowColor = color;
        ctx.shadowBlur = 6;
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.4;
        ctx.strokeRect(x1, y + 4, bw, Math.max(4, rowHeight - 8));
        ctx.restore();
        ctx.setLineDash([]);

        ctx.fillStyle = color;
        ctx.globalAlpha = 0.26;
        ctx.fillRect(x1, y + 4, bw, Math.max(4, rowHeight - 8));
        ctx.globalAlpha = 1;

        drawBurnMarkers(ctx, x1, x2, y, rowHeight, color, true);
      }
    }

    const sweepX = timeToX(nowEpochS);
    if (sweepX >= LABEL_WIDTH && sweepX <= lw - 10) {
      ctx.save();
      ctx.strokeStyle = 'rgba(88, 184, 255, 0.16)';
      ctx.lineWidth = 8;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, plotBottom);
      ctx.stroke();

      ctx.strokeStyle = 'rgba(88, 184, 255, 0.34)';
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, plotBottom);
      ctx.stroke();

      ctx.strokeStyle = theme.colors.primary;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(sweepX, HEADER_HEIGHT);
      ctx.lineTo(sweepX, plotBottom);
      ctx.stroke();
      ctx.restore();
    }

    const legendY = lh - 14;
    const legendItems = [
      { label: 'Avoidance', color: theme.colors.accent },
      { label: 'Recovery', color: theme.colors.warning },
      { label: 'Graveyard', color: '#8892a0' },
      { label: 'Pending', color: theme.colors.primary },
      { label: 'Cooldown', color: 'rgba(88, 184, 255, 0.72)' },
    ];

    let legendX = LABEL_WIDTH;
    ctx.font = `10px ${theme.font.mono}`;
    ctx.textAlign = 'left';
    for (const item of legendItems) {
      if (item.label === 'Cooldown') {
        ctx.strokeStyle = item.color;
        ctx.setLineDash([3, 2]);
        ctx.strokeRect(legendX, legendY - 7, 12, 8);
        ctx.setLineDash([]);
      } else {
        ctx.fillStyle = item.color;
        ctx.fillRect(legendX, legendY - 7, 12, 8);
      }
      ctx.fillStyle = theme.colors.textMuted;
      ctx.fillText(item.label, legendX + 16, legendY + 1);
      legendX += ctx.measureText(item.label).width + 32;
    }

    ctx.restore();
  }, [burns, selectedSatId, nowEpochS]);

  drawRef.current = draw;

  useEffect(() => {
    draw();
  }, [draw]);

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
      style={{ flex: 1, width: '100%', height: '100%', minHeight: 0, display: 'block' }}
    />
  );
});
