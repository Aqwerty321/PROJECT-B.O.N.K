// Utility functions for orbital visualization math

/** Convert geodetic lat/lon to Mercator pixel coordinates on a canvas */
export function latLonToMercator(
  lat: number,
  lon: number,
  width: number,
  height: number
): [number, number] {
  // Clamp latitude to valid Mercator range
  const clampedLat = Math.max(-85, Math.min(85, lat));
  const x = ((lon + 180) / 360) * width;
  const latRad = (clampedLat * Math.PI) / 180;
  const mercN = Math.log(Math.tan(Math.PI / 4 + latRad / 2));
  const y = (height / 2) - (width * mercN) / (2 * Math.PI);
  return [x, y];
}

/**
 * Compute terminator line points (day/night boundary).
 * Returns an array of [x, y] pixel pairs tracing the terminator.
 * Uses a simplified solar declination based on day-of-year derived from ISO timestamp.
 */
export function computeTerminator(
  timestamp: string,
  width: number,
  height: number,
  steps = 180
): [number, number][] {
  const date = new Date(timestamp);
  const dayOfYear = Math.floor(
    (date.getTime() - new Date(date.getUTCFullYear(), 0, 0).getTime()) / 86400000
  );
  // Solar declination (degrees)
  const decl = -23.45 * Math.cos((2 * Math.PI * (dayOfYear + 10)) / 365);
  const declRad = (decl * Math.PI) / 180;

  // UTC fraction of day → Greenwich Hour Angle
  const utcHours = date.getUTCHours() + date.getUTCMinutes() / 60 + date.getUTCSeconds() / 3600;
  const gha = (utcHours / 24) * 360 - 180; // degrees, -180..180

  const points: [number, number][] = [];
  for (let i = 0; i <= steps; i++) {
    const lon = -180 + (i / steps) * 360;
    const lonRad = ((lon - gha) * Math.PI) / 180;
    // latitude of terminator at this longitude
    const lat = (Math.atan(-Math.cos(lonRad) / Math.tan(declRad)) * 180) / Math.PI;
    const [px, py] = latLonToMercator(lat, lon, width, height);
    points.push([px, py]);
  }
  return points;
}

/** Format seconds as HH:MM:SS */
export function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

/** Convert a numeric hex color (e.g. 0x22c55e) to a CSS hex string */
export function hexColor(n: number): string {
  return `#${n.toString(16).padStart(6, '0')}`;
}

/** Fuel fraction 0..1 → CSS hsl colour (green → yellow → red) */
export function fuelColor(fraction: number): string {
  if (fraction > 0.5) {
    // green → yellow
    const t = (1 - fraction) * 2;
    const r = Math.round(t * 234);
    const g = 197;
    return `rgb(${r}, ${g}, 8)`;
  } else {
    // yellow → red
    const t = fraction * 2;
    const g = Math.round(t * 179);
    return `rgb(239, ${g}, 68)`;
  }
}
