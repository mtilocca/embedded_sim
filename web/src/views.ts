import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { h, fmt, ago, uptime } from './dom';
import { signals, devices, log, isPaused, setPaused, notify, Sig } from './store';
import { MqttSettings, loadMqtt, saveMqtt, applyMqtt } from './sources';

export interface View {
  title: string;
  el: HTMLElement;
  render(): void;
}

const byKey = (a: Sig, b: Sig) => a.key.localeCompare(b.key);
// rt = timing table, bearing_ai = dedicated tab (bearing.ts)
const plain = () => [...signals.values()].filter((s) => s.sim !== 'rt' && s.sim !== 'bearing_ai').sort(byKey);

// ---------------------------------------------------------------- Overview
export function overview(): View {
  const el = h('section', { class: 'view' });
  const tiles = new Map<string, { root: HTMLElement; val: HTMLElement; meta: HTMLElement }>();
  const empty = h('p', { class: 'muted' }, 'Waiting for data from the device...');
  const grid = h('div', { class: 'grid' });
  const devs = h('div', { class: 'devices' });
  el.append(devs, grid, empty);

  return {
    title: 'Overview',
    el,
    render() {
      devs.replaceChildren(
        ...[...devices.values()].map((d) =>
          h(
            'div',
            { class: 'card dev' },
            h('strong', {}, d.device),
            h('span', { class: `dot ${d.state === 'online' ? 'ok' : 'bad'}` }),
            h('span', { class: 'muted' }, `${d.board}  up ${uptime(d.uptime_ms)}`),
          ),
        ),
      );
      const list = plain();
      empty.hidden = list.length > 0;
      for (const s of list) {
        let t = tiles.get(s.key);
        if (!t) {
          const val = h('div', { class: 'big' });
          const meta = h('div', { class: 'muted small' });
          const root = h('div', { class: 'card tile' }, h('div', { class: 'small' }, `${s.sim} / ${s.signal}`), val, meta);
          grid.append(root);
          t = { root, val, meta };
          tiles.set(s.key, t);
        }
        t.val.textContent = `${fmt(s.value)} ${s.unit}`;
        t.root.dataset.q = s.quality;
        t.meta.textContent = `${s.device}  ${s.quality}  ${ago(s.rx)}`;
      }
    },
  };
}

// ------------------------------------------------------------------ Charts
const COLORS = ['#38bdf8', '#f59e0b', '#a78bfa', '#34d399', '#f87171', '#f472b6'];

export function charts(): View {
  const el = h('section', { class: 'view' });
  const plots = new Map<string, { u: uPlot; n: number }>();
  const empty = h('p', { class: 'muted' }, 'No numeric signals yet.');
  const grid = h('div', { class: 'charts' });
  el.append(grid, empty);
  let color = 0;

  function make(s: Sig, host: HTMLElement): uPlot {
    const stroke = COLORS[color++ % COLORS.length];
    const css = getComputedStyle(document.documentElement);
    const axis = css.getPropertyValue('--muted').trim() || '#888';
    const gridc = css.getPropertyValue('--line').trim() || '#333';
    return new uPlot(
      {
        width: Math.max(host.clientWidth, 280),
        height: 200,
        legend: { show: false },
        cursor: { drag: { x: false, y: false } },
        scales: { x: { time: true } },
        axes: [
          { stroke: axis, grid: { stroke: gridc, width: 1 }, ticks: { stroke: gridc } },
          { stroke: axis, grid: { stroke: gridc, width: 1 }, ticks: { stroke: gridc }, size: 56 },
        ],
        series: [{}, { label: s.signal, stroke, width: 2, points: { show: false } }],
      },
      [s.xs, s.ys],
      host,
    );
  }

  new ResizeObserver(() => {
    for (const p of plots.values()) p.u.setSize({ width: Math.max(p.u.root.parentElement?.clientWidth ?? 280, 280), height: 200 });
  }).observe(el);

  return {
    title: 'Charts',
    el,
    render() {
      if (el.offsetParent === null) return; // hidden tab: uPlot cannot size itself
      const list = plain();
      empty.hidden = list.length > 0;
      for (const s of list) {
        let p = plots.get(s.key);
        if (!p) {
          const host = h('div', { class: 'plot' });
          grid.append(h('div', { class: 'card' }, h('div', { class: 'small' }, `${s.device} / ${s.sim} / ${s.signal} [${s.unit}]`), host));
          p = { u: make(s, host), n: -1 };
          plots.set(s.key, p);
        }
        if (p.n !== s.xs.length || !isPaused()) {
          p.u.setData([s.xs, s.ys]);
          p.n = s.xs.length;
        }
      }
    },
  };
}

// -------------------------------------------------------------- Real-time
export function realtime(): View {
  const el = h('section', { class: 'view' });
  const body = h('tbody');
  el.append(
    h('p', { class: 'muted' }, 'Per-task timing of the real-time core. over = missed deadlines. Jitter is limited by the kernel tick.'),
    h('table', { class: 'tbl' }, h('thead', {}, h('tr', {}, ...['device', 'task', 'jitter max (us)', 'exec avg (us)', 'overruns', 'seen'].map((c) => h('th', {}, c)))), body),
  );

  return {
    title: 'Real-time',
    el,
    render() {
      const rows = new Map<string, Record<string, Sig>>();
      for (const s of signals.values()) {
        if (s.sim !== 'rt') continue;
        const i = s.signal.lastIndexOf('_');
        const metricStart = s.signal.endsWith('jitter_max') ? s.signal.length - 'jitter_max'.length - 1 : s.signal.endsWith('exec_avg') ? s.signal.length - 'exec_avg'.length - 1 : i;
        const task = `${s.device}\u0000${s.signal.slice(0, metricStart)}`;
        const metric = s.signal.slice(metricStart + 1);
        (rows.get(task) ?? rows.set(task, {}).get(task)!)[metric] = s;
      }
      body.replaceChildren(
        ...[...rows.entries()].sort().map(([k, m]) => {
          const [device, task] = k.split('\u0000');
          const over = m.overruns?.value ?? 0;
          const seen = Math.max(...Object.values(m).map((x) => x.rx));
          return h(
            'tr',
            {},
            h('td', {}, device),
            h('td', {}, task),
            h('td', {}, m.jitter_max ? fmt(m.jitter_max.value) : '-'),
            h('td', {}, m.exec_avg ? fmt(m.exec_avg.value) : '-'),
            h('td', { class: over > 0 ? 'bad-text' : '' }, String(over)),
            h('td', { class: 'muted' }, ago(seen)),
          );
        }),
      );
    },
  };
}

// ----------------------------------------------------------------- Console
export function console_(): View {
  const el = h('section', { class: 'view' });
  const filter = h('input', { type: 'search', placeholder: 'filter topic or payload', class: 'inp' }) as HTMLInputElement;
  const pause = h('button', { class: 'btn', onclick: () => { setPaused(!isPaused()); pause.textContent = isPaused() ? 'Resume' : 'Pause'; notify(); } }, 'Pause');
  const out = h('pre', { class: 'console' });
  el.append(h('div', { class: 'bar' }, filter, pause, h('button', { class: 'btn', onclick: () => { log.length = 0; notify(); } }, 'Clear')), out);

  return {
    title: 'Console',
    el,
    render() {
      const f = filter.value.trim().toLowerCase();
      const lines = log
        .filter((e) => !f || e.topic.toLowerCase().includes(f) || e.payload.toLowerCase().includes(f))
        .slice(-150)
        .map((e) => `${new Date(e.rx).toLocaleTimeString()} [${e.source}] ${e.topic} ${e.payload}`);
      out.textContent = lines.join('\n');
      out.scrollTop = out.scrollHeight;
    },
  };
}

// ---------------------------------------------------------------- Settings
export function settings(): View {
  const el = h('section', { class: 'view' });
  const s: MqttSettings = loadMqtt();
  const field = (label: string, key: keyof MqttSettings, type = 'text', ph = '') => {
    const i = h('input', { type, placeholder: ph, class: 'inp', value: String(s[key]) }) as HTMLInputElement;
    i.addEventListener('input', () => ((s[key] as string) = i.value));
    return h('label', { class: 'field' }, h('span', {}, label), i);
  };
  const en = h('input', { type: 'checkbox' }) as HTMLInputElement;
  en.checked = s.enabled;
  en.addEventListener('change', () => (s.enabled = en.checked));

  el.append(
    h('div', { class: 'card form' },
      h('h3', {}, 'MQTT broker (optional second source)'),
      h('p', { class: 'muted' }, 'The device WebSocket is always on and needs nothing else. Add a broker to also see other devices. Mosquitto needs a WebSocket listener (docs/web.md). Stored in this browser only.'),
      h('label', { class: 'field' }, h('span', {}, 'Enabled'), en),
      field('Broker URL', 'url', 'text', 'ws://192.168.1.147:9001'),
      field('Username', 'user'),
      field('Password', 'password', 'password'),
      field('Topic filter', 'topic'),
      h('button', { class: 'btn primary', onclick: () => { saveMqtt(s); applyMqtt(s); } }, 'Save & connect'),
    ),
  );
  return { title: 'Settings', el, render() {} };
}
