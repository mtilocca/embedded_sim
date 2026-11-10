import './style.css';
import { h } from './dom';
import { conn, subscribe, notify } from './store';
import { startDeviceSocket, applyMqtt, loadMqtt } from './sources';
import { bearingAi } from './bearing';
import { overview, charts, realtime, console_, settings, View } from './views';

const views: View[] = [overview(), charts(), bearingAi(), realtime(), console_(), settings()];
let active = 0;

const chip = (name: string) => h('span', { class: 'chip' }, h('span', { class: 'dot' }), h('span', {}, name));
const devChip = chip('Device');
const mqttChip = chip('Broker');

const tabs = views.map((v, i) => h('button', { class: 'tab', onclick: () => select(i) }, v.title));
const main = h('main', {}, ...views.map((v) => v.el));

document.getElementById('app')!.append(
  h('header', {}, h('h1', {}, 'embedded_sim'), h('nav', {}, ...tabs), h('div', { class: 'chips' }, devChip, mqttChip)),
  main,
);

function select(i: number): void {
  active = i;
  views.forEach((v, j) => (v.el.hidden = j !== i));
  tabs.forEach((t, j) => t.classList.toggle('on', j === i));
  notify();
}

function paintChip(el: HTMLElement, state: string): void {
  const cls = state === 'connected' ? 'ok' : state === 'off' ? 'off' : state === 'connecting' ? 'warn' : 'bad';
  el.querySelector('.dot')!.className = `dot ${cls}`;
  el.title = state;
}

subscribe(() => {
  paintChip(devChip, conn.device);
  paintChip(mqttChip, conn.mqtt);
  views[active].render();
});

select(0);
startDeviceSocket();
applyMqtt(loadMqtt());
setInterval(notify, 1000); // refresh "x s ago" labels
