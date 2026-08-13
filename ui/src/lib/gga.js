export const GGA_QUALITY = {
  0: 'Invalid',
  1: 'GPS (Standalone)',
  2: 'DGPS',
  3: 'PPS',
  4: 'RTK Fixed',
  5: 'RTK Float',
  6: 'Estimated',
  7: 'Manual',
  8: 'Simulation',
};

export function ggaQualityLabel(quality) {
  if (quality == null) return 'No GGA';
  return GGA_QUALITY[quality] ?? `Unknown (${quality})`;
}

export function ggaQualityColor(quality) {
  if (quality == null) return '#475569';
  if (quality === 4) return '#22c55e';
  if (quality === 5) return '#d97706';
  if (quality === 2 || quality === 3) return '#60a5fa';
  if (quality === 1) return '#94a3b8';
  return '#a855f7';
}
