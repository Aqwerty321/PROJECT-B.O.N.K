import { useEffect, useRef, useCallback, memo } from 'react';
import type { BurnsResponse, ExecutedBurn, PendingBurn } from '../types/api';
import { theme } from '../styles/theme';

interface ManeuverGanttProps {
  burns: BurnsResponse | null;
  selectedSatId: string | null;
  nowEpochS: number;
  onSelectBurn?: (burn: ExecutedBurn | PendingBurn) => void;
  onExposeHitRegions?: (regions: Array<{ burnId: string; x: number; y: number }>) => void;
}

interface SatLaneSummary {
  executed: number;
  pending: number;
  dropped: number;
  predictive: number;
}

const HEADER_HEIGHT = 34;
const FOOTER_HEIGHT = 34;
const LABEL_WIDTH = 110;
const DESIRED_ROW_HEIGHT = 32;
const MIN_ROW_HEIGHT = 6;
const MAX_ROW_HEIGHT = 36;
const MIN_BLOCK_WIDTH = 8;
const BURN_DURATION_S = 120;
const COOLDOWN_DURATION_S = 600;
const MIN_PLOT_WIDTH = 960;
const MIN_PX_PER_HOUR = 240;
const MIN_PX_PER_EVENT = 38;

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

function burnLabel(burn: ExecutedBurn | PendingBurn): string {
  if (burn.graveyard_burn) return 'GRAVEYARD';
  if (burn.recovery_burn) return 'RECOVERY';
  if (burn.scheduled_from_predictive_cdm) return 'AUTO-COLA';
  if (burn.auto_generated) return 'AUTO';
  return 'MANUAL';
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

function drawStateMarker(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  rowHeight: number,
  color: string,
  dash: number[],
  label?: string,
) {
  ctx.save();
  ctx.setLineDash(dash);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.3;
  ctx.beginPath();
  ctx.moveTo(x, y + 1);
  ctx.lineTo(x, y + rowHeight - 1);
  ctx.stroke();
  ctx.setLineDash([]);

  if (label) {
    ctx.fillStyle = color;
    ctx.font = `9px ${theme.font.mono}`;
    ctx.textAlign = 'left';
    ctx.fillText(label, x + 4, y + 10);
  }
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

function drawWrappedText(
  ctx: CanvasRenderingContext2D,
  text: string,
  x: number,
  y: number,
  maxWidth: number,
  lineHeight: number,
): number {
  const words = text.split(' ');
  const lines: string[] = [];
  let current = '';

  for (const word of words) {
    const next = current ? `${current} ${word}` : word;
    if (current && ctx.measureText(next).width > maxWidth) {
      lines.push(current);
      current = word;
    } else {
      current = next;
    }
  }

  if (current) {
    lines.push(current);
  }

  lines.forEach((line, index) => {
    ctx.fillText(line, x, y + index * lineHeight);
  });

  return lines.length;
}

function drawEmptyState(
  ctx: CanvasRenderingContext2D,
  chartWidth: number,
  selectedSatId: string | null,
) {
  const top = HEADER_HEIGHT + 22;
  const contentX = LABEL_WIDTH + 18;
  const descriptionWidth = Math.min(chartWidth - 42, 420);
  const laneWidth = Math.min(Math.max(360, chartWidth * 0.54), chartWidth - 48);
  const laneX = LABEL_WIDTH + Math.max(24, Math.floor((chartWidth - laneWidth) / 2));
  const burnWidth = Math.max(84, Math.min(164, chartWidth * 0.12));
  const cooldownWidth = Math.max(72, Math.min(136, (COOLDOWN_DURATION_S / 3600) * chartWidth * 0.45));
  const gap = 28;
  const rowY = top + 46;
  const sampleY = rowY + 8;
  const burnX = laneX + 24;
  const cooldownX = burnX + burnWidth + gap;
  const markerX = cooldownX + cooldownWidth + gap + 8;
  const labelY = sampleY + 36;

  ctx.save();
  ctx.fillStyle = 'rgba(226, 232, 240, 0.22)';
  ctx.font = `12px ${theme.font.mono}`;
  ctx.textAlign = 'left';
  ctx.fillText(selectedSatId ? `${selectedSatId} NO BURNS QUEUED` : 'FLEET BURN CLOCK IDLE', contentX, top);

  ctx.fillStyle = 'rgba(148, 163, 184, 0.62)';
  ctx.font = `10px ${theme.font.mono}`;
  const descriptionLines = drawWrappedText(
    ctx,
    'This lane populates when evasion, recovery, or graveyard burns enter the command queue.',
    contentX,
    top + 18,
    descriptionWidth,
    13,
  );

  const laneGuideY = top + 18 + descriptionLines * 13 + 18;
  ctx.strokeStyle = 'rgba(88, 184, 255, 0.14)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(laneX, laneGuideY);
  ctx.lineTo(laneX + laneWidth, laneGuideY);
  ctx.stroke();

  ctx.fillStyle = 'rgba(57, 217, 138, 0.28)';
  ctx.fillRect(burnX, sampleY, burnWidth, 20);
  ctx.strokeStyle = 'rgba(57, 217, 138, 0.78)';
  ctx.strokeRect(burnX, sampleY, burnWidth, 20);

  ctx.setLineDash([4, 3]);
  ctx.strokeStyle = 'rgba(88, 184, 255, 0.78)';
  ctx.strokeRect(cooldownX, sampleY, cooldownWidth, 20);
  ctx.setLineDash([]);

  ctx.strokeStyle = 'rgba(255, 98, 98, 0.75)';
  ctx.setLineDash([2, 3]);
  ctx.beginPath();
  ctx.moveTo(markerX, sampleY - 6);
  ctx.lineTo(markerX, sampleY + 28);
  ctx.stroke();
  ctx.setLineDash([]);

  ctx.fillStyle = 'rgba(226, 232, 240, 0.45)';
  ctx.textAlign = 'center';
  ctx.fillText('Burn Window', burnX + burnWidth / 2, labelY);
  ctx.fillText('Cooldown Hold', cooldownX + cooldownWidth / 2, labelY);
  ctx.fillText('Conflict / blackout marker', markerX + 56, labelY);
  ctx.restore();
}

export default memo(function ManeuverGantt({ burns, selectedSatId, nowEpochS, onSelectBurn, onExposeHitRegions }: ManeuverGanttProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const drawRef = useRef<() => void>(() => {});
  const hitRegionsRef = useRef<Array<{ x1: number; x2: number; y1: number; y2: number; burn: ExecutedBurn | PendingBurn }>>([]);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    const scrollContainer = scrollRef.current;
    if (!canvas || !scrollContainer) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const containerWidth = scrollContainer.offsetWidth;
    const containerHeight = scrollContainer.offsetHeight;
    if (containerWidth === 0 || containerHeight === 0) return;

    const baselinePlotWidth = Math.max(280, containerWidth - LABEL_WIDTH - 12);

    if (!burns || (burns.executed.length === 0 && burns.pending.length === 0 && (burns.dropped?.length ?? 0) === 0)) {
      hitRegionsRef.current = [];
      onExposeHitRegions?.([]);
      // For empty state, canvas = container size
      canvas.style.width = `${containerWidth}px`;
      canvas.style.height = `${containerHeight}px`;
      canvas.width = containerWidth * dpr;
      canvas.height = containerHeight * dpr;
      ctx.save();
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, containerWidth, containerHeight);
      drawEmptyGrid(ctx, containerWidth, containerHeight, baselinePlotWidth);
      drawEmptyState(ctx, baselinePlotWidth, selectedSatId);
      ctx.restore();
      return;
    }

    const allExecuted = burns.executed;
    const allPending = burns.pending;
    const allDropped = burns.dropped ?? [];

    const satIds = new Set<string>();
    allExecuted.forEach(burn => satIds.add(burn.satellite_id));
    allPending.forEach(burn => satIds.add(burn.satellite_id));
    allDropped.forEach(burn => satIds.add(burn.satellite_id));
    const satellites = Array.from(satIds).sort();
    const laneSummaryBySat = new Map<string, SatLaneSummary>();
    for (const satId of satellites) {
      laneSummaryBySat.set(satId, { executed: 0, pending: 0, dropped: 0, predictive: 0 });
    }
    allExecuted.forEach(burn => {
      const lane = laneSummaryBySat.get(burn.satellite_id);
      if (!lane) return;
      lane.executed += 1;
      lane.predictive += burn.scheduled_from_predictive_cdm ? 1 : 0;
    });
    allPending.forEach(burn => {
      const lane = laneSummaryBySat.get(burn.satellite_id);
      if (!lane) return;
      lane.pending += 1;
      lane.predictive += burn.scheduled_from_predictive_cdm ? 1 : 0;
    });
    allDropped.forEach(burn => {
      const lane = laneSummaryBySat.get(burn.satellite_id);
      if (!lane) return;
      lane.dropped += 1;
      lane.predictive += burn.scheduled_from_predictive_cdm ? 1 : 0;
    });

    if (satellites.length === 0) {
      hitRegionsRef.current = [];
      onExposeHitRegions?.([]);
      canvas.style.width = `${containerWidth}px`;
      canvas.style.height = `${containerHeight}px`;
      canvas.width = containerWidth * dpr;
      canvas.height = containerHeight * dpr;
      ctx.save();
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, containerWidth, containerHeight);
      drawEmptyGrid(ctx, containerWidth, containerHeight, baselinePlotWidth);
      drawEmptyState(ctx, baselinePlotWidth, selectedSatId);
      ctx.restore();
      return;
    }

    const parseEpoch = (iso: string) => new Date(iso).getTime() / 1000;
    const allEpochs = [
      ...allExecuted.map(burn => parseEpoch(burn.burn_epoch)),
      ...allPending.map(burn => parseEpoch(burn.burn_epoch)),
      ...allDropped.map(burn => parseEpoch(burn.burn_epoch)),
    ];

    if (allEpochs.length === 0) {
      hitRegionsRef.current = [];
      onExposeHitRegions?.([]);
      canvas.style.width = `${containerWidth}px`;
      canvas.style.height = `${containerHeight}px`;
      canvas.width = containerWidth * dpr;
      canvas.height = containerHeight * dpr;
      ctx.save();
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, containerWidth, containerHeight);
      drawEmptyGrid(ctx, containerWidth, containerHeight, baselinePlotWidth);
      drawEmptyState(ctx, baselinePlotWidth, selectedSatId);
      ctx.restore();
      return;
    }

    // Compute ideal row height: use DESIRED_ROW_HEIGHT, but allow shrinking to fit
    const availableRowArea = containerHeight - HEADER_HEIGHT - FOOTER_HEIGHT - 8;
    const fittedRowHeight = Math.floor(availableRowArea / Math.max(satellites.length, 1));
    const rowHeight = Math.max(MIN_ROW_HEIGHT, Math.min(MAX_ROW_HEIGHT, Math.max(fittedRowHeight, DESIRED_ROW_HEIGHT)));

    // If rows don't fit, make the canvas taller (vertical scroll)
    const neededRowArea = rowHeight * satellites.length + 8;
    const canvasLogicalHeight = Math.max(containerHeight, neededRowArea + HEADER_HEIGHT + FOOTER_HEIGHT);

    const tMin = Math.min(...allEpochs) - 600;
    const tMax = Math.max(...allEpochs) + 600;
    const tRange = Math.max(tMax - tMin, 3600);
    const eventCount = allExecuted.length + allPending.length + allDropped.length;
    const desiredPlotWidth = Math.max(
      baselinePlotWidth,
      MIN_PLOT_WIDTH,
      Math.ceil((tRange / 3600) * MIN_PX_PER_HOUR),
      Math.ceil(eventCount * MIN_PX_PER_EVENT),
    );

    // Expand logical canvas width so horizontal scroll keeps the timeline readable.
    const lw = LABEL_WIDTH + desiredPlotWidth + 12;
    const lh = canvasLogicalHeight;
    canvas.style.width = `${lw}px`;
    canvas.style.height = `${lh}px`;
    canvas.width = lw * dpr;
    canvas.height = lh * dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, lw, lh);
    hitRegionsRef.current = [];
    const exposedRegions: Array<{ burnId: string; x: number; y: number }> = [];

    const plotBottom = drawPanelChrome(ctx, lw, lh);
    const timeToX = (t: number) => LABEL_WIDTH + ((t - tMin) / tRange) * desiredPlotWidth;

    const rowAreaHeight = Math.max(72, plotBottom - HEADER_HEIGHT - 8);
    const rowsHeight = rowHeight * satellites.length;
    const rowsTop = HEADER_HEIGHT + Math.max(0, Math.floor((rowAreaHeight - rowsHeight) / 2));
    const labelFontSize = Math.max(7, Math.min(11, rowHeight * 0.7));

    ctx.fillStyle = 'rgba(153, 169, 188, 0.78)';
    ctx.font = `10px ${theme.font.mono}`;
    ctx.textAlign = 'center';

    const tickCount = Math.max(6, Math.min(20, Math.floor(desiredPlotWidth / 140)));
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
        ctx.fillStyle = 'rgba(88, 184, 255, 0.10)';
        ctx.fillRect(0, y, lw, rowHeight);
        ctx.fillStyle = 'rgba(88, 184, 255, 0.85)';
        ctx.fillRect(0, y, 3, rowHeight);
        ctx.strokeStyle = 'rgba(88, 184, 255, 0.24)';
        ctx.lineWidth = 1;
        ctx.strokeRect(0.5, y + 0.5, lw - 1, Math.max(1, rowHeight - 1));
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
      if (rowHeight >= 18) {
        const lane = laneSummaryBySat.get(satId);
        if (lane) {
          const laneLabel = `${lane.executed}/${lane.pending}${lane.dropped > 0 ? `/${lane.dropped}D` : ''}${lane.predictive > 0 ? ` P${lane.predictive}` : ''}`;
          ctx.fillStyle = 'rgba(153, 169, 188, 0.52)';
          ctx.font = `8px ${theme.font.mono}`;
          ctx.textAlign = 'right';
          ctx.fillText(laneLabel, LABEL_WIDTH - 8, y + rowHeight - 4);
        }
      }

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

        hitRegionsRef.current.push({
          x1,
          x2,
          y1: y + 4,
          y2: y + Math.max(8, rowHeight - 4),
          burn,
        });
        exposedRegions.push({ burnId: burn.id, x: Math.round((x1 + x2) / 2), y: Math.round(y + rowHeight / 2) });

        drawBurnMarkers(ctx, x1, x2, y, rowHeight, color, false);

        if (rowHeight >= 16) {
          ctx.fillStyle = 'rgba(6, 8, 12, 0.86)';
          ctx.font = `9px ${theme.font.mono}`;
          ctx.textAlign = 'left';
          ctx.fillText(burnLabel(burn), x1 + 4, y + Math.max(13, rowHeight * 0.58));
        }

        if (burn.collision_avoided && rowHeight >= 14) {
          ctx.fillStyle = theme.colors.accent;
          ctx.font = `8px ${theme.font.mono}`;
          ctx.textAlign = 'right';
          ctx.fillText('AVOIDED', Math.min(lw - 6, x2 + 44), y + Math.max(11, rowHeight * 0.42));
        }

        if (burn.blackout_overlap) {
          drawStateMarker(ctx, x1 - 3, y, rowHeight, theme.colors.warning, [2, 3], rowHeight >= 18 ? 'B/O' : undefined);
        }
        if (burn.command_conflict || burn.cooldown_conflict) {
          drawStateMarker(ctx, x2 + 2, y, rowHeight, theme.colors.critical, [4, 3], rowHeight >= 18 ? 'CONFLICT' : undefined);
        }
        if (burn.scheduled_from_predictive_cdm && burn.trigger_tca_epoch_s) {
          const triggerX = timeToX(burn.trigger_tca_epoch_s);
          if (triggerX >= LABEL_WIDTH && triggerX <= lw - 4) {
            drawStateMarker(ctx, triggerX, y, rowHeight, theme.colors.primary, [1, 3], rowHeight >= 18 ? 'TCA' : undefined);
          }
        }
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

        hitRegionsRef.current.push({
          x1,
          x2,
          y1: y + 4,
          y2: y + Math.max(8, rowHeight - 4),
          burn,
        });
        exposedRegions.push({ burnId: burn.id, x: Math.round((x1 + x2) / 2), y: Math.round(y + rowHeight / 2) });

        drawBurnMarkers(ctx, x1, x2, y, rowHeight, color, true);

        if (rowHeight >= 16) {
          ctx.fillStyle = 'rgba(6, 8, 12, 0.86)';
          ctx.font = `9px ${theme.font.mono}`;
          ctx.textAlign = 'left';
          ctx.fillText(burnLabel(burn), x1 + 4, y + Math.max(13, rowHeight * 0.58));
        }

        if (burn.blackout_overlap) {
          drawStateMarker(ctx, x1 - 3, y, rowHeight, theme.colors.warning, [2, 3], rowHeight >= 18 ? 'B/O' : undefined);
        }
        if (burn.command_conflict || burn.cooldown_conflict) {
          drawStateMarker(ctx, x2 + 2, y, rowHeight, theme.colors.critical, [4, 3], rowHeight >= 18 ? 'CONFLICT' : undefined);
        }
        if (burn.scheduled_from_predictive_cdm && burn.trigger_tca_epoch_s) {
          const triggerX = timeToX(burn.trigger_tca_epoch_s);
          if (triggerX >= LABEL_WIDTH && triggerX <= lw - 4) {
            drawStateMarker(ctx, triggerX, y, rowHeight, theme.colors.primary, [1, 3], rowHeight >= 18 ? 'TCA' : undefined);
          }
        }
      }

      const satDropped = allDropped.filter(burn => burn.satellite_id === satId);
      for (const burn of satDropped) {
        const t = parseEpoch(burn.burn_epoch);
        const x = timeToX(t);
        drawStateMarker(ctx, x, y, rowHeight, theme.colors.critical, [2, 2], rowHeight >= 18 ? 'DROP' : undefined);
        if (burn.blackout_overlap) {
          drawStateMarker(ctx, x - 5, y, rowHeight, theme.colors.warning, [1, 3]);
        }
      }
    }

    onExposeHitRegions?.(exposedRegions);

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
      { label: 'Avoided', color: theme.colors.accent },
      { label: 'Cooldown', color: 'rgba(88, 184, 255, 0.72)' },
      { label: 'Blackout', color: theme.colors.warning },
      { label: 'Conflict', color: theme.colors.critical },
      { label: 'Dropped', color: theme.colors.critical },
    ];

    let legendX = LABEL_WIDTH;
    const legendFont = `10px ${theme.font.mono}`;
    ctx.font = legendFont;
    ctx.textAlign = 'left';
    for (const item of legendItems) {
      if (item.label === 'Cooldown') {
        ctx.strokeStyle = item.color;
        ctx.setLineDash([3, 2]);
        ctx.strokeRect(legendX, legendY - 7, 12, 8);
        ctx.setLineDash([]);
      } else if (item.label === 'Avoided') {
        ctx.fillStyle = item.color;
        ctx.font = `8px ${theme.font.mono}`;
        ctx.fillText('OK', legendX, legendY + 1);
        ctx.font = legendFont;
      } else if (item.label === 'Blackout') {
        ctx.strokeStyle = item.color;
        ctx.setLineDash([2, 3]);
        ctx.beginPath();
        ctx.moveTo(legendX + 6, legendY - 8);
        ctx.lineTo(legendX + 6, legendY + 1);
        ctx.stroke();
        ctx.setLineDash([]);
      } else if (item.label === 'Conflict' || item.label === 'Dropped') {
        ctx.strokeStyle = item.color;
        ctx.setLineDash(item.label === 'Dropped' ? [2, 2] : [4, 3]);
        ctx.beginPath();
        ctx.moveTo(legendX + 6, legendY - 8);
        ctx.lineTo(legendX + 6, legendY + 1);
        ctx.stroke();
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
  }, [burns, selectedSatId, nowEpochS, onExposeHitRegions]);

  drawRef.current = draw;

  useEffect(() => {
    draw();
  }, [draw]);

  useEffect(() => {
    const container = scrollRef.current;
    if (!container) return;
    const obs = new ResizeObserver(() => {
      drawRef.current();
    });
    obs.observe(container);
    return () => obs.disconnect();
  }, []);

  useEffect(() => {
    const container = scrollRef.current;
    const canvas = canvasRef.current;
    if (!container || !canvas || !onSelectBurn) return;

    const handleClick = (event: MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left + container.scrollLeft;
      const y = event.clientY - rect.top + container.scrollTop;
      const hit = hitRegionsRef.current.find(region => x >= region.x1 && x <= region.x2 && y >= region.y1 && y <= region.y2);
      if (hit) {
        onSelectBurn(hit.burn);
      }
    };

    const handlePointerMove = (event: MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      const x = event.clientX - rect.left + container.scrollLeft;
      const y = event.clientY - rect.top + container.scrollTop;
      const hit = hitRegionsRef.current.some(region => x >= region.x1 && x <= region.x2 && y >= region.y1 && y <= region.y2);
      canvas.style.cursor = hit ? 'pointer' : 'default';
    };

    canvas.addEventListener('click', handleClick);
    canvas.addEventListener('mousemove', handlePointerMove);
    return () => {
      canvas.removeEventListener('click', handleClick);
      canvas.removeEventListener('mousemove', handlePointerMove);
      canvas.style.cursor = 'default';
    };
  }, [onSelectBurn]);

  return (
    <div
      ref={scrollRef}
      style={{
        flex: 1,
        width: '100%',
        height: '100%',
        minHeight: 0,
        overflowY: 'auto',
        overflowX: 'auto',
      }}
    >
      <canvas
        ref={canvasRef}
        style={{ display: 'block', minWidth: '100%' }}
      />
    </div>
  );
});
