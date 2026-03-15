/**
 * RSSI to signal level (0-4 bars).
 */
export function signalLevel(rssi) {
  if (rssi == null) return 0;
  return rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : rssi > -80 ? 1 : 0;
}

/**
 * RSSI to human-readable label.
 */
export function signalLabel(rssi) {
  const level = signalLevel(rssi);
  return level >= 4 ? 'Excellent' : level === 3 ? 'Good' : level === 2 ? 'Fair' : level >= 1 ? 'Weak' : 'None';
}

/**
 * Full signal string: "Excellent (-42 dBm)"
 */
export function formatSignal(rssi) {
  if (rssi == null) return 'N/A';
  return `${signalLabel(rssi)} (${rssi} dBm)`;
}

/**
 * Signal color CSS variable based on level.
 */
export function signalColor(rssi) {
  const level = signalLevel(rssi);
  return level >= 3 ? 'var(--success)' : level === 2 ? 'var(--warning)' : 'var(--danger)';
}

/**
 * WiFi arcs SVG with dimmed unused arcs (header icon replacement).
 * @param {number|null} rssi - null for disconnected
 * @param {number} size - icon size in px
 */
export function wifiArcsSvg(rssi, size = 14) {
  const level = signalLevel(rssi);
  const color = rssi != null ? signalColor(rssi) : 'var(--muted-foreground)';
  const dim = 'var(--muted-foreground)" opacity="0.25';
  const arc3 = level >= 4 ? color : `${dim}`;
  const arc2 = level >= 3 ? color : `${dim}`;
  const arc1 = level >= 2 ? color : `${dim}`;
  const dot = level >= 1 ? color : `${dim}`;
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M1.42 9a16 16 0 0 1 21.16 0" stroke="${arc3}"/>
    <path d="M5 12.55a11 11 0 0 1 14.08 0" stroke="${arc2}"/>
    <path d="M8.53 16.11a6 6 0 0 1 6.95 0" stroke="${arc1}"/>
    <circle cx="12" cy="20" r="1" fill="${dot}"/>
  </svg>`;
}

/**
 * Signal bars SVG for cards and settings.
 * @param {number|null} rssi
 * @param {number} height - bar area height
 * @param {number} barWidth - individual bar width
 */
export function signalBarsSvg(rssi, height = 28, barWidth = 5) {
  const level = signalLevel(rssi);
  const color = signalColor(rssi);
  const dim = 'var(--muted-foreground)" opacity="0.25';
  const gap = barWidth + 2;
  const w = gap * 4 - 2;
  const h1 = Math.round(height * 0.25);
  const h2 = Math.round(height * 0.5);
  const h3 = Math.round(height * 0.75);
  const h4 = height;
  return `<svg width="${w}" height="${height}" viewBox="0 0 ${w} ${height}" style="vertical-align:middle">
    <rect x="0" y="${height - h1}" width="${barWidth}" height="${h1}" rx="1" fill="${level >= 1 ? color : `${dim}`}"/>
    <rect x="${gap}" y="${height - h2}" width="${barWidth}" height="${h2}" rx="1" fill="${level >= 2 ? color : `${dim}`}"/>
    <rect x="${gap * 2}" y="${height - h3}" width="${barWidth}" height="${h3}" rx="1" fill="${level >= 3 ? color : `${dim}`}"/>
    <rect x="${gap * 3}" y="${height - h4}" width="${barWidth}" height="${h4}" rx="1" fill="${level >= 4 ? color : `${dim}`}"/>
  </svg>`;
}

/**
 * Tooltip text for WiFi (4.C style: "SSID · -22 dBm").
 */
export function wifiTooltip(ssid, rssi) {
  if (rssi == null) return ssid ? `${ssid} · disconnected` : 'WiFi: disconnected';
  return `${ssid || 'WiFi'} · ${rssi} dBm`;
}
