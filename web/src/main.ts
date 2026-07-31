import './style.css';
import { display, type Instruction, type Summary, type Variant } from './model';

type Snapshot = Record<string, string | number> & { edge: number };
type Trace = {
    instruction: Instruction;
    rs1: string;
    rs2: string;
    shift: number;
    snapshots: Snapshot[];
    response_edge: number;
    vcd: string;
};
type Catalog = {
    traces: { id: string; instruction: Instruction; file: string }[];
};

const baseUrl = import.meta.env?.BASE_URL ?? './';
const asset = (name: string) => `${baseUrl}evidence/${name}`;
const element = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;
const value = (id: string) => element<HTMLInputElement>(id).value;
const escape = (input: unknown) => String(input).replace(/[&<>"']/g, character => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
}[character]!));

async function read<T>(name: string): Promise<T>
{
    const response = await fetch(asset(name));
    if (!response.ok) throw new Error(`missing evidence ${name}`);
    return response.json();
}

const variants: Variant[] = ['baseline', 'fqmul', 'red32', 'fsri'];
const labels: Record<Variant, string> = {
    baseline: 'Baseline', fqmul: 'FQMUL', red32: 'RED32', fsri: 'FSRI',
};
const formulas: Record<Instruction, string> = {
    fqmul: 'signed low half multiply followed by Montgomery reduction',
    red32: 'Montgomery reduction of the product already held in the first source register',
    fsri: 'low word of the joined source registers shifted right by the immediate',
};
const encodings: Record<Instruction, string> = {
    fqmul: 'mask 0xfe00707f  match 0x0000000b',
    red32: 'mask 0xfe00707f  match 0x0000100b',
    fsri: 'mask 0xc000707f  match 0x0000200b',
};

let summary: Summary;
let catalog: Catalog;
let worker: Worker;
let request = 0;
let trace: Trace | undefined;
let traceIndex = 0;
let timer: ReturnType<typeof setInterval> | undefined;

function calculate()
{
    const id = ++request;
    worker.postMessage({
        id,
        kind: value('instruction'),
        a: value('rs1'),
        b: value('rs2'),
        shift: Number(value('shift')),
    });
}

function instructionChanged()
{
    const instruction = value('instruction') as Instruction;
    element('formula').textContent = formulas[instruction];
    element('encoding').textContent = encodings[instruction];
    element<HTMLInputElement>('shift').disabled = instruction !== 'fsri';
    element<HTMLImageElement>('schematic').src = asset(`${instruction}-schematic.svg`);
    element<HTMLAnchorElement>('wrapper').href = asset(`${instruction}.h`);
    const entries = catalog.traces.filter(entry => entry.instruction === instruction);
    const select = element<HTMLSelectElement>('trace-select');
    select.innerHTML = entries.map(entry =>
        `<option value="${escape(entry.file)}">${escape(entry.id)}</option>`).join('');
    select.disabled = entries.length === 0;
    void loadTrace();
    calculate();
}

function stopTrace()
{
    clearInterval(timer);
    timer = undefined;
}

function showTrace()
{
    if (!trace) return;
    const snapshot = trace.snapshots[traceIndex];
    element('trace-edge').textContent = `snapshot ${snapshot.edge}`;
    element('trace-data').textContent = JSON.stringify(snapshot, null, 2);
    element<HTMLButtonElement>('previous').disabled = traceIndex === 0;
    element<HTMLButtonElement>('next').disabled = traceIndex === trace.snapshots.length - 1;
}

async function loadTrace()
{
    stopTrace();
    const file = value('trace-select');
    if (!file)
    {
        trace = undefined;
        element('trace-status').textContent = 'no current Verilator trace is checked in';
        element('trace-data').textContent = 'run scripts/workbench_data.py with Verilator installed';
        return;
    }
    trace = await read<Trace>(file);
    traceIndex = 0;
    element('trace-status').textContent =
        `response at snapshot ${trace.response_edge}  waveform ${trace.vcd}`;
    showTrace();
}

function renderResults()
{
    const level = value('level');
    element('cycle-rows').innerHTML = variants.map(variant => {
        const result = summary.measurements[variant][level];
        return `<tr><th>${labels[variant]}</th><td>${display(result.keygen_cycles)}</td>` +
            `<td>${display(result.encapsulation_cycles)}</td>` +
            `<td>${display(result.decapsulation_cycles)}</td>` +
            `<td>${display(result.total_cycles)}</td>` +
            `<td>${display(result.percent_change_vs_baseline, '%')}</td>` +
            `<td>${result.verified ? 'yes' : 'pending rerun'}</td></tr>`;
    }).join('');

    element('hardware-rows').innerHTML = variants.map(variant => {
        const result = summary.hardware[variant];
        const seeds = result.fmax_by_seed_mhz.length
            ? result.fmax_by_seed_mhz.join('  ')
            : 'pending rerun';
        return `<tr><th>${labels[variant]}</th><td>${display(result.lut4)}</td>` +
            `<td>${display(result.flip_flops)}</td><td>${display(result.dsp)}</td>` +
            `<td>${display(result.bram)}</td><td>${display(result.median_fmax_mhz, ' MHz')}</td>` +
            `<td>${escape(seeds)}</td></tr>`;
    }).join('');
}

async function main()
{
    [summary, catalog] = await Promise.all([
        read<Summary>('summary.json'), read<Catalog>('catalog.json'),
    ]);
    element('app').innerHTML = `
      <header><p class="eyebrow">PicoRV32 ML KEM instruction experiment</p><h1>Baseline versus three instructions</h1>
      <p>One fixed software structure isolates FQMUL RED32 and direct FSRI</p>
      <nav><a href="#instruction-view">Instruction</a><a href="#results-view">Results</a><a href="#evidence-view">RTL and evidence</a></nav></header>
      <main id="main">
        <section id="instruction-view"><h2>Instruction</h2><div class="controls">
          <label>instruction<select id="instruction"><option value="fqmul">FQMUL</option><option value="red32">RED32</option><option value="fsri">FSRI</option></select></label>
          <label>first source<input id="rs1" value="0x00008000"></label>
          <label>second source<input id="rs2" value="0x00000680"></label>
          <label>shift<input id="shift" type="number" min="0" max="31" value="13"></label>
        </div><p id="formula"></p><code id="encoding"></code><pre id="arithmetic"></pre>
        <div class="two"><article><h3>PCPI playback</h3><select id="trace-select"></select><p id="trace-status"></p>
          <div class="buttons"><button id="previous">previous</button><button id="next">next</button><button id="play">play</button></div>
          <h4 id="trace-edge"></h4><pre id="trace-data"></pre></article>
          <article><h3>Word level schematic</h3><img id="schematic" alt="selected instruction schematic">
          <p><a id="wrapper">C wrapper</a>  <a href="${asset('pqc_pcpi_mlkem.sv')}">current RTL</a></p></article></div></section>
        <section id="results-view"><h2>Results</h2><p class="notice">Summary status  <strong>${escape(summary.status)}</strong></p>
          <p>The old planner measurements are intentionally absent because their software schedules do not define this comparison</p>
          <label>parameter set<select id="level"><option>512</option><option>768</option><option>1024</option></select></label>
          <div class="table-scroll"><table><thead><tr><th>variant</th><th>keygen cycles</th><th>encapsulation cycles</th><th>decapsulation cycles</th><th>total cycles</th><th>change</th><th>verified</th></tr></thead><tbody id="cycle-rows"></tbody></table></div>
          <h3>Complete core ECP5 result</h3><div class="table-scroll"><table><thead><tr><th>variant</th><th>LUT4</th><th>FF</th><th>DSP</th><th>BRAM</th><th>median Fmax</th><th>seed values</th></tr></thead><tbody id="hardware-rows"></tbody></table></div>
        </section>
        <section id="evidence-view"><h2>RTL and evidence</h2><div class="two"><article><h3>Fixed comparison</h3>
          <p>${escape(summary.baseline)}</p><p>mlkem native  <code>${summary.pins.mlkem_native}</code></p><p>PicoRV32  <code>${summary.pins.picorv32}</code></p>
          <p>Every CPU uses the same project PCPI multiplier for ordinary RV32M operations</p></article>
          <article><h3>Formal scope</h3><p>The three property files check arbitrary instruction operands and the expected response inside short bounded traces</p>
          <p>They are local PCPI checks and do not verify the processor or ML KEM</p>
          <p><a href="${asset('fqmul_properties.sv')}">FQMUL property</a>  <a href="${asset('red32_properties.sv')}">RED32 property</a>  <a href="${asset('fsri_properties.sv')}">FSRI property</a></p></article></div>
          <p><a href="${asset('catalog.json')}">evidence catalog</a>  <a href="${asset('summary.json')}">machine readable summary</a>  <a href="${asset('schematics.json')}">schematic provenance</a></p>
        </section>
      </main>`;

    worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = event => {
        if (event.data.id !== request) return;
        if (event.data.error)
        {
            element('arithmetic').textContent = event.data.error;
            element('arithmetic').className = 'error';
            return;
        }
        element('arithmetic').className = '';
        element('arithmetic').textContent = Object.entries(event.data.result)
            .map(([name, result]) => `${name.padEnd(16)} ${result}`)
            .join('\n');
    };
    element('instruction').onchange = instructionChanged;
    for (const id of ['rs1', 'rs2', 'shift']) element(id).oninput = calculate;
    element('trace-select').onchange = () => void loadTrace();
    element('previous').onclick = () => { stopTrace(); traceIndex--; showTrace(); };
    element('next').onclick = () => { stopTrace(); traceIndex++; showTrace(); };
    element('play').onclick = () => {
        stopTrace();
        if (!trace) return;
        traceIndex = 0;
        showTrace();
        timer = setInterval(() => {
            if (!trace || traceIndex === trace.snapshots.length - 1)
            {
                stopTrace();
                return;
            }
            traceIndex++;
            showTrace();
        }, 500);
    };
    element('level').onchange = renderResults;
    instructionChanged();
    renderResults();
}

void main().catch(error => {
    element('app').innerHTML = `<main><h1>Evidence unavailable</h1><p>${escape(error)}</p></main>`;
});
