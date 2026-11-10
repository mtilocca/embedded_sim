// "Bearing AI" tab: live view of the on-device int8 CNN (sims/bearing_ai).
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { h, fmt, ago } from './dom';
import { signals, Sig } from './store';
import type { View } from './views';

export const AI_SIM = 'bearing_ai';
const CLASSES = ['normal', 'inner race', 'ball', 'outer race'];
const PROBS = ['p_normal', 'p_inner', 'p_ball', 'p_outer'];
const COLORS = ['#22c55e', '#f59e0b', '#a78bfa', '#f87171'];

// signals of the first device that runs the AI sim
function pick(): Record<string, Sig> {
  const out: Record<string, Sig> = {};
  let dev = '';
  for (const s of signals.values()) {
    if (s.sim !== AI_SIM) continue;
    dev ||= s.device;
    if (s.device === dev) out[s.signal] = s;
  }
  return out;
}

// align several histories on the shortest one (a dropped sample must not skew the chart)
function align(list: Sig[]): [number[], ...number[][]] {
  const n = Math.min(...list.map((s) => s.ys.length));
  return [list[0].xs.slice(-n), ...list.map((s) => s.ys.slice(-n))];
}

export function bearingAi(): View {
  const el = h('section', { class: 'view' });
  const empty = h('p', { class: 'muted' }, 'Waiting for the bearing_ai simulation (STM32 board, CONFIG_SIM_BEARING_AI)...');

  const truthV = h('div', { class: 'big' });
  const predV = h('div', { class: 'big' });
  const verdict = h('div', { class: 'small' });
  const scoreV = h('div', { class: 'big' });
  const scoreFill = h('div', { class: 'meter-fill' });
  const inferV = h('div', { class: 'big' });
  const rmsV = h('div', { class: 'big' });
  const meta = h('div', { class: 'muted small' });

  const tile = (title: string, ...c: (HTMLElement | null)[]) => h('div', { class: 'card tile' }, h('div', { class: 'small' }, title), ...c);
  const top = h('div', { class: 'grid' },
    tile('Machine condition (scripted ground truth)', truthV),
    tile('Model prediction (on-device)', predV, verdict),
    tile('Anomaly score = 1 - P(normal)', scoreV, h('div', { class: 'meter' }, scoreFill)),
    tile('Inference time (TFLM, int8)', inferV),
    tile('Vibration RMS', rmsV),
  );

  const bars = PROBS.map((_, i) => {
    const fill = h('div', { class: 'pbar-fill' });
    fill.style.background = COLORS[i];
    const val = h('span', { class: 'pbar-val' });
    return { fill, val, row: h('div', { class: 'pbar' }, h('span', { class: 'pbar-name' }, CLASSES[i]), h('div', { class: 'pbar-track' }, fill), val) };
  });
  const probCard = h('div', { class: 'card' }, h('div', { class: 'small' }, 'Class probabilities'), ...bars.map((b) => b.row));

  const hostP = h('div', { class: 'plot' });
  const hostC = h('div', { class: 'plot' });
  const charts = h('div', { class: 'charts' },
    h('div', { class: 'card' }, h('div', { class: 'small' }, 'Class probabilities over time'), hostP),
    h('div', { class: 'card' }, h('div', { class: 'small' }, 'Ground truth vs prediction (0 normal, 1 inner, 2 ball, 3 outer)'), hostC),
  );
  const body = h('div', {}, top, h('div', { class: 'ai-row' }, probCard, h('p', { class: 'muted small ai-note' },
    'A scripted machine cycles normal, inner-race, normal, ball, normal, outer-race faults. ' +
    'Each 100 ms the device replays a real accelerometer window (CWRU dataset, motor load never seen in training), ' +
    'normalises it and classifies it on the STM32U5 with an int8 CNN. See python/ for training and evaluation.'), meta), charts);
  el.append(empty, body);
  body.hidden = true;

  let up: uPlot | null = null;
  let uc: uPlot | null = null;
  const css = () => getComputedStyle(document.documentElement);
  const axes = () => {
    const a = css().getPropertyValue('--muted').trim() || '#888';
    const g = css().getPropertyValue('--line').trim() || '#333';
    return [{ stroke: a, grid: { stroke: g, width: 1 }, ticks: { stroke: g } }, { stroke: a, grid: { stroke: g, width: 1 }, ticks: { stroke: g }, size: 44 }];
  };
  const opts = (host: HTMLElement, series: uPlot.Series[], extra: Partial<uPlot.Options> = {}): uPlot.Options => ({
    width: Math.max(host.clientWidth, 280), height: 200, legend: { show: false }, cursor: { drag: { x: false, y: false } },
    scales: { x: { time: true } }, axes: axes(), series: [{}, ...series], ...extra,
  });
  new ResizeObserver(() => {
    for (const [u, host] of [[up, hostP], [uc, hostC]] as const) u?.setSize({ width: Math.max(host.clientWidth, 280), height: 200 });
  }).observe(el);

  return {
    title: 'Bearing AI',
    el,
    render() {
      const s = pick();
      const have = PROBS.every((k) => s[k]) && s.predicted_class && s.true_class;
      empty.hidden = !!have;
      body.hidden = !have;
      if (!have) return;

      const truth = Math.round(s.true_class.value);
      const pred = Math.round(s.predicted_class.value);
      truthV.textContent = CLASSES[truth] ?? '?';
      predV.textContent = CLASSES[pred] ?? '?';
      verdict.textContent = pred === truth ? 'correct' : truth === 0 || pred === 0 ? 'wrong (anomaly mismatch)' : 'anomaly found, wrong fault type';
      verdict.className = `small ${pred === truth ? 'ok-text' : 'bad-text'}`;
      predV.className = `big ${pred === 0 ? 'ok-text' : 'bad-text'}`;
      const sc = s.anomaly_score?.value ?? 0;
      scoreV.textContent = sc.toFixed(3);
      scoreFill.style.width = `${Math.min(100, sc * 100)}%`;
      scoreFill.style.background = sc < 0.5 ? 'var(--ok)' : 'var(--bad)';
      inferV.textContent = s.infer_us ? `${fmt(s.infer_us.value)} us` : '-';
      rmsV.textContent = s.vib_rms ? `${fmt(s.vib_rms.value)} g` : '-';
      meta.textContent = `${s.true_class.device}, updated ${ago(s.true_class.rx)}`;
      PROBS.forEach((k, i) => {
        bars[i].fill.style.width = `${s[k].value * 100}%`;
        bars[i].val.textContent = `${(s[k].value * 100).toFixed(1)}%`;
      });

      if (el.offsetParent === null) return;
      const pl = PROBS.map((k) => s[k]);
      const cl = [s.true_class, s.predicted_class];
      const pdata = align(pl);
      const cdata = align(cl);
      if (!up) {
        up = new uPlot(opts(hostP, PROBS.map((_, i) => ({ stroke: COLORS[i], width: 2, points: { show: false } })), { scales: { x: { time: true }, y: { range: [0, 1] } } }), pdata, hostP);
        uc = new uPlot(opts(hostC, [
          { stroke: '#8b98a8', width: 2, points: { show: false }, paths: uPlot.paths.stepped!({ align: 1 }) },
          { stroke: '#38bdf8', width: 2, dash: [6, 4], points: { show: false }, paths: uPlot.paths.stepped!({ align: 1 }) },
        ], { scales: { x: { time: true }, y: { range: [-0.2, 3.2] } } }), cdata, hostC);
      } else {
        up.setData(pdata);
        uc!.setData(cdata);
      }
    },
  };
}
