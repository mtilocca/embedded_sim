import mqtt, { MqttClient } from 'mqtt';
import { ingest, setConn } from './store';

// ---- Source 1: the device itself (always available, no server needed) ----
export function startDeviceSocket(): void {
  const url = `ws://${location.host}/ws`;
  const open = () => {
    setConn('device', 'connecting');
    const ws = new WebSocket(url);
    ws.onopen = () => setConn('device', 'connected');
    ws.onmessage = (ev) => {
      try {
        const m = JSON.parse(String(ev.data)) as { topic: string; p: unknown };
        ingest(m.topic, JSON.stringify(m.p), 'device');
      } catch {
        /* ignore malformed frame */
      }
    };
    ws.onclose = () => {
      setConn('device', 'disconnected');
      setTimeout(open, 2000);
    };
    ws.onerror = () => ws.close();
  };
  open();
}

// ---- Source 2: optional MQTT broker over WebSocket (Mosquitto listener) ----
export interface MqttSettings {
  enabled: boolean;
  url: string; // ws://host:9001
  user: string;
  password: string;
  topic: string;
}

const KEY = 'embedded_sim.mqtt';
export const defaultMqtt: MqttSettings = { enabled: false, url: '', user: '', password: '', topic: 'embedded_sim/#' };

export function loadMqtt(): MqttSettings {
  try {
    return { ...defaultMqtt, ...(JSON.parse(localStorage.getItem(KEY) ?? '{}') as Partial<MqttSettings>) };
  } catch {
    return { ...defaultMqtt };
  }
}

export function saveMqtt(s: MqttSettings): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(s));
  } catch {
    /* storage unavailable */
  }
}

let client: MqttClient | null = null;

export function applyMqtt(s: MqttSettings): void {
  client?.end(true);
  client = null;
  if (!s.enabled || !s.url) {
    setConn('mqtt', 'off');
    return;
  }
  setConn('mqtt', 'connecting');
  const c = mqtt.connect(s.url, {
    username: s.user || undefined,
    password: s.password || undefined,
    clientId: `web-${Math.random().toString(16).slice(2, 10)}`,
    reconnectPeriod: 3000,
    connectTimeout: 5000,
    clean: true,
  });
  client = c;
  c.on('connect', () => {
    setConn('mqtt', 'connected');
    c.subscribe(s.topic || 'embedded_sim/#');
  });
  c.on('reconnect', () => setConn('mqtt', 'connecting'));
  c.on('close', () => setConn('mqtt', 'disconnected'));
  c.on('error', () => setConn('mqtt', 'error'));
  c.on('message', (topic, payload) => ingest(topic, payload.toString(), 'mqtt'));
}
