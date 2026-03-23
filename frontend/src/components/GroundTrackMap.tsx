import React, { useEffect, useRef, useCallback } from 'react';
import type { VisualizationSnapshot, SatelliteSnapshot, TrajectoryResponse } from '../types/api';
import { latLonToMercator, computeTerminator, hexColor } from '../utils/geo';
import { GROUND_STATIONS, statusColor } from '../types/api';
import { theme } from '../styles/theme';

interface Props {
  snapshot: VisualizationSnapshot | null;
  selectedSatId: string | null;
  onSelectSat: (id: string | null) => void;
  trackHistory: Map<string, [number, number][]>;
  trackVersion: number;  // triggers redraw without cloning the Map
  trajectory: TrajectoryResponse | null;
}

// World map colors
const BG_OCEAN  = '#0a1628';
const COLOR_GRID = 'rgba(255,255,255,0.06)';
const COLOR_TERMINATOR = 'rgba(0,0,0,0.45)';
const COLOR_DEBRIS = 'rgba(239,68,68,0.55)';
const COLOR_GS = '#7dd3fc';

function drawWrappedPath(
  ctx: CanvasRenderingContext2D,
  points: Array<[number, number]>,
  w: number,
  h: number,
  options: { color: string; width: number; dash?: number[]; alpha?: number },
) {
  if (points.length < 2) return;

  ctx.save();
  ctx.strokeStyle = options.color;
  ctx.lineWidth = options.width;
  ctx.globalAlpha = options.alpha ?? 1;
  if (options.dash) ctx.setLineDash(options.dash);
  ctx.beginPath();

  let firstPoint = true;
  let prevX = 0;
  for (const [lat, lon] of points) {
    const [px, py] = latLonToMercator(lat, lon, w, h);
    if (firstPoint) {
      ctx.moveTo(px, py);
      firstPoint = false;
    } else if (Math.abs(px - prevX) > w * 0.5) {
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(px, py);
    } else {
      ctx.lineTo(px, py);
    }
    prevX = px;
  }

  ctx.stroke();
  ctx.setLineDash([]);
  ctx.restore();
}

// Simple Mercator world outline -- draw graticule lines
function drawGraticule(ctx: CanvasRenderingContext2D, w: number, h: number) {
  ctx.strokeStyle = COLOR_GRID;
  ctx.lineWidth = 0.5;

  for (let lon = -180; lon <= 180; lon += 30) {
    const [x] = latLonToMercator(0, lon, w, h);
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  for (let lat = -60; lat <= 60; lat += 30) {
    const [, y] = latLonToMercator(lat, 0, w, h);
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  // Equator -- slightly brighter
  ctx.strokeStyle = 'rgba(255,255,255,0.15)';
  ctx.lineWidth = 0.8;
  const [, yeq] = latLonToMercator(0, 0, w, h);
  ctx.beginPath();
  ctx.moveTo(0, yeq);
  ctx.lineTo(w, yeq);
  ctx.stroke();
}

function drawTerminator(
  ctx: CanvasRenderingContext2D,
  timestamp: string,
  w: number,
  h: number
) {
  const pts = computeTerminator(timestamp, w, h, 360);
  if (pts.length < 2) return;

  ctx.save();
  // shade nightside
  ctx.fillStyle = COLOR_TERMINATOR;
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length; i++) {
    ctx.lineTo(pts[i][0], pts[i][1]);
  }
  // close the top or bottom based on declination
  ctx.lineTo(w, 0);
  ctx.lineTo(0, 0);
  ctx.closePath();
  ctx.fill();

  // terminator line
  ctx.strokeStyle = 'rgba(251,191,36,0.5)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length; i++) {
    ctx.lineTo(pts[i][0], pts[i][1]);
  }
  ctx.stroke();
  ctx.restore();
}

function drawGroundStations(ctx: CanvasRenderingContext2D, w: number, h: number) {
  ctx.save();
  ctx.strokeStyle = COLOR_GS;
  ctx.fillStyle = COLOR_GS;
  ctx.lineWidth = 1;
  for (const gs of GROUND_STATIONS) {
    const [x, y] = latLonToMercator(gs.lat, gs.lon, w, h);
    // Just dots for compact mini-map (no labels to reduce clutter)
    ctx.beginPath();
    ctx.arc(x, y, 2.5, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function drawEmptyMapState(ctx: CanvasRenderingContext2D, w: number, h: number) {
  ctx.save();

  const pad = 22;
  const legendY = h - 18;

  ctx.strokeStyle = 'rgba(58, 159, 232, 0.14)';
  ctx.lineWidth = 1;
  ctx.strokeRect(pad, pad, w - pad * 2, h - pad * 2);

  ctx.strokeStyle = 'rgba(58, 159, 232, 0.12)';
  ctx.setLineDash([8, 6]);
  ctx.beginPath();
  ctx.moveTo(pad, h * 0.5);
  ctx.lineTo(w - pad, h * 0.5);
  ctx.stroke();
  ctx.setLineDash([]);

  ctx.fillStyle = 'rgba(226, 232, 240, 0.12)';
  ctx.font = `11px ${theme.font.mono}`;
  ctx.textAlign = 'center';
  ctx.fillText('GROUND TRACK STANDBY', w / 2, h * 0.42);

  ctx.fillStyle = 'rgba(148, 163, 184, 0.55)';
  ctx.font = `9px ${theme.font.mono}`;
  ctx.fillText('Awaiting live snapshot / 90-minute trail / forecast path', w / 2, h * 0.42 + 18);

  ctx.textAlign = 'left';
  ctx.fillStyle = 'rgba(58, 159, 232, 0.72)';
  ctx.fillRect(pad, legendY - 5, 10, 2);
  ctx.fillStyle = 'rgba(226, 232, 240, 0.42)';
  ctx.fillText('Historical trail', pad + 16, legendY);

  ctx.strokeStyle = 'rgba(234, 179, 8, 0.72)';
  ctx.setLineDash([5, 4]);
  ctx.beginPath();
  ctx.moveTo(pad + 118, legendY - 4);
  ctx.lineTo(pad + 136, legendY - 4);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.fillStyle = 'rgba(226, 232, 240, 0.42)';
  ctx.fillText('Predicted path', pad + 142, legendY);

  ctx.fillStyle = 'rgba(125, 211, 252, 0.9)';
  ctx.beginPath();
  ctx.arc(w - pad - 126, legendY - 4, 2.5, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = 'rgba(226, 232, 240, 0.42)';
  ctx.fillText('Ground stations', w - pad - 116, legendY);

  ctx.restore();
}

export const GroundTrackMap = React.memo(function GroundTrackMap({ snapshot, selectedSatId, onSelectSat, trackHistory, trackVersion, trajectory }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const drawRef = useRef<() => void>(() => {});

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.width / dpr;
    const h = canvas.height / dpr;

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // Clear
    ctx.fillStyle = BG_OCEAN;
    ctx.fillRect(0, 0, w, h);

    // Background land-mass approximation gradient
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, 'rgba(17,34,64,0.0)');
    grad.addColorStop(0.5, 'rgba(17,34,64,0.6)');
    grad.addColorStop(1, 'rgba(17,34,64,0.0)');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);

    drawGraticule(ctx, w, h);

    if (snapshot) {
      drawTerminator(ctx, snapshot.timestamp, w, h);
    }

    drawGroundStations(ctx, w, h);

    if (!snapshot) {
      drawEmptyMapState(ctx, w, h);
      ctx.restore();
      return;
    }

    // --- Debris cloud (draw first, under satellites) ---
    ctx.save();
    for (const d of snapshot.debris_cloud) {
      const [, lat, lon] = d;
      const [x, y] = latLonToMercator(lat, lon, w, h);
      ctx.fillStyle = COLOR_DEBRIS;
      ctx.fillRect(x - 0.6, y - 0.6, 1.2, 1.2);
    }
    ctx.restore();

    const trajectoryTargetId = trajectory?.satellite_id ?? null;

    // --- Satellite tracks ---
    for (const sat of snapshot.satellites) {
      if (trajectoryTargetId && sat.id === trajectoryTargetId) continue;
      const history = trackHistory.get(sat.id);
      if (history && history.length > 1) {
        drawWrappedPath(ctx, history, w, h, {
          color: `${hexColor(statusColor(sat.status))}88`,
          width: 0.8,
          dash: [3, 2],
          alpha: 0.8,
        });
      }
    }

    if (trajectory?.trail && trajectory.trail.length > 1) {
      drawWrappedPath(
        ctx,
        trajectory.trail.map(point => [point.lat, point.lon]),
        w,
        h,
        {
          color: 'rgba(58, 159, 232, 0.95)',
          width: 1.8,
          alpha: 0.95,
        },
      );
    }

    if (trajectory?.predicted && trajectory.predicted.length > 1) {
      drawWrappedPath(
        ctx,
        trajectory.predicted.map(point => [point.lat, point.lon]),
        w,
        h,
        {
          color: 'rgba(234, 179, 8, 0.95)',
          width: 1.3,
          dash: [6, 4],
          alpha: 0.95,
        },
      );
    }

    // --- Satellites ---
    for (const sat of snapshot.satellites) {
      const [x, y] = latLonToMercator(sat.lat, sat.lon, w, h);
      const isSelected = sat.id === selectedSatId;
      const col = hexColor(statusColor(sat.status));

      if (isSelected) {
        // selection ring
        ctx.save();
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(x, y, 6, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();
      }

      // Glow
      const glow = ctx.createRadialGradient(x, y, 0, x, y, 5);
      glow.addColorStop(0, `${col}cc`);
      glow.addColorStop(1, `${col}00`);
      ctx.fillStyle = glow;
      ctx.beginPath();
      ctx.arc(x, y, 5, 0, Math.PI * 2);
      ctx.fill();

      // Core dot
      ctx.fillStyle = col;
      ctx.beginPath();
      ctx.arc(x, y, 2.5, 0, Math.PI * 2);
      ctx.fill();

      // Label for selected (compact)
      if (isSelected) {
        ctx.font = `8px ${theme.font.mono}`;
        ctx.fillStyle = '#ffffff';
        ctx.fillText(sat.id, x + 5, y - 4);
      }
    }

    ctx.restore();
  }, [snapshot, selectedSatId, trackHistory, trackVersion, trajectory]);

  // Keep drawRef pointing at the latest draw function
  drawRef.current = draw;

  // Redraw when data changes (no RAF loop needed -- data updates at ~1Hz)
  useEffect(() => {
    draw();
  }, [draw]);

  // Handle canvas resize (DPR-aware) — uses drawRef to avoid re-subscribing
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const obs = new ResizeObserver(() => {
      const curDpr = window.devicePixelRatio || 1;
      canvas.width = canvas.offsetWidth * curDpr;
      canvas.height = canvas.offsetHeight * curDpr;
      drawRef.current();
    });
    obs.observe(canvas);
    canvas.width = canvas.offsetWidth * dpr;
    canvas.height = canvas.offsetHeight * dpr;
    return () => obs.disconnect();
  }, []);

  // Click-to-select satellite
  const handleClick = useCallback(
    (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!snapshot) return;
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.width / dpr;
      const h = canvas.height / dpr;

      let closest: SatelliteSnapshot | null = null;
      let closestDist = 12; // px hit radius
      for (const sat of snapshot.satellites) {
        const [sx, sy] = latLonToMercator(sat.lat, sat.lon, w, h);
        const d = Math.hypot(mx - sx, my - sy);
        if (d < closestDist) {
          closestDist = d;
          closest = sat;
        }
      }
      onSelectSat(closest ? closest.id : null);
    },
    [snapshot, onSelectSat]
  );

  return (
    <canvas
      ref={canvasRef}
      onClick={handleClick}
      style={{
        width: '100%',
        height: '100%',
        display: 'block',
        cursor: 'crosshair',
      }}
    />
  );
});
