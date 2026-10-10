// Single source of truth for everything the UI shows. Both data sources
// (device WebSocket, MQTT-over-WebSocket) feed ingest() with the same
// {topic, payload} shape, see docs/mqtt.md.

export type Source = 'device' | 'mqtt';

export interface Sig {
  key: string; // device/sim/signal
  device: string;
  sim: string;
  signal: string;
  value: number;
  unit: string;
  quality: string;
  t: number; // device uptime ms
  rx: number; // browser receive time, ms epoch
  xs: number[]; // history x: seconds epoch (uPlot time scale)
  ys: number[];
}

export interface DeviceStatus {
  device: string;
  state: string;
  board: string;
  uptime_ms: number;
  rx: number;
}

export interface LogEntry {
  rx: number;
  source: Source;
  topic: string;
  payload: string;
}

const HISTORY = 600;
const LOG_MAX = 300;
const ROOT = 'embedded_sim';

export const signals = new Map<string, Sig>();
export const devices = new Map<string, DeviceStatus>();
export const log: LogEntry[] = [];
export const conn: Record<Source, string> = { device: 'connecting', mqtt: 'off' };

let paused = false;
export const setPaused = (p: boolean) => (paused = p);
export const isPaused = () => paused;

const listeners = new Set<() => void>();
let queued = false;

export function subscribe(fn: () => void): void {
  listeners.add(fn);
}

export function notify(): void {
  if (queued) return;
  queued = true;
  requestAnimationFrame(() => {
    queued = false;
    listeners.forEach((fn) => fn());
  });
}

export function setConn(source: Source, state: string): void {
  conn[source] = state;
  notify();
}

export function ingest(topic: string, text: string, source: Source): void {
  const rx = Date.now();
  if (!paused) {
    log.push({ rx, source, topic, payload: text });
    if (log.length > LOG_MAX) log.splice(0, log.length - LOG_MAX);
  }

  const parts = topic.split('/');
  if (parts[0] !== ROOT) return;
  let p: Record<string, unknown>;
  try {
    p = JSON.parse(text) as Record<string, unknown>;
  } catch {
    return;
  }

  if (parts.length === 3 && parts[2] === 'status') {
    devices.set(parts[1], {
      device: parts[1],
      state: String(p.state ?? '?'),
      board: String(p.board ?? ''),
      uptime_ms: Number(p.uptime_ms ?? 0),
      rx,
    });
  } else if (parts.length === 4 && typeof p.v === 'number') {
    const [, device, sim, signal] = parts;
    const key = `${device}/${sim}/${signal}`;
    let s = signals.get(key);
    if (!s) {
      s = { key, device, sim, signal, value: 0, unit: '', quality: 'good', t: -1, rx: 0, xs: [], ys: [] };
      signals.set(key, s);
    }
    const t = Number(p.t ?? 0);
    // Same sample arriving via both sources (same device uptime): keep one.
    if (t > 0 && t <= s.t) return;
    s.value = p.v;
    s.unit = typeof p.u === 'string' ? p.u : '';
    s.quality = typeof p.q === 'string' ? p.q : 'good';
    s.t = t;
    s.rx = rx;
    if (!paused || s.xs.length === 0) {
      s.xs.push(rx / 1000);
      s.ys.push(p.v);
      if (s.xs.length > HISTORY) {
        s.xs.splice(0, s.xs.length - HISTORY);
        s.ys.splice(0, s.ys.length - HISTORY);
      }
    }
  } else {
    return;
  }
  notify();
}
