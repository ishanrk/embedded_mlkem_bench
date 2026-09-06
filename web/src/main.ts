import './style.css';
import { operand, hex, signed, failures, original_budget, type Instruction, type Hardware, type Budget } from './model';

type Snapshot = { edge: number; state: number } & Record<string, string | number>;
type Trace = { instruction: Instruction; variant: string; rs1: string; rs2: string; shift: number; encoding: string;
    source: string; source_sha256: string; source_revision: string | null; parameters: Record<string, number>;
    snapshots: Snapshot[]; accepted_edge: number; response_edge: number; sampling: string; latency: string;
    status: string; tool: string; vcd: string };
type Catalog = { traces: { id: string; variant: string; file: string }[]; inputs: Record<string, string>; artifacts: Record<string, string>; repository_sha: string; wasm: string };
type Summary = { levels: Record<string, Record<string, number> & { operation_cycles: Record<string, Record<string, number>> }>;
    hardware: Record<string, Hardware> };
type Measurement = { cycles: Record<string, number>; instructions: Record<string, number>; source: string; plan_id: string; status: string };
const names: Record<string, string> = { portable: 'Portable software', software: 'Optimized software', fqmul: 'FQMUL', red32: 'RED32',
    fsri_combinational: 'FSRI · historical direct', fsri_multiplier_reuse: 'FSRI · multiplier reuse', fsri_sliced: 'FSRI · experimental sliced', fsri_direct: 'FSRI · integrated direct (new run)' };
const states = ['IDLE', 'PRODUCT', 'INVERSE', 'MODULUS', 'RESPONSE'];
const base_url = import.meta.env?.BASE_URL ?? './';
const asset = (name: string) => `${base_url}evidence/${name}`;
const escape = (value: unknown) => String(value).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]!));
const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;
const value = (id: string) => $<HTMLInputElement>(id).value;
const link = (name: string, label: string) => `<a href="${asset(name)}" target="_blank" rel="noopener">${escape(label)}</a>`;
const source_link = (path: string) => `https://github.com/ishanrk/embedded_mlkem_bench/blob/${catalog.repository_sha}/${path}`;
const options = (items: Record<string, string>) => Object.entries(items).map(([k, v]) => `<option value="${escape(k)}">${escape(v)}</option>`).join('');
async function read<T>(name: string): Promise<T>
{
    const response = await fetch(asset(name));
    if (!response.ok) throw new Error(`Evidence unavailable: ${name}`);
    return response.json();
}
let catalog: Catalog;
let summary: Summary;
let measurements: { levels: Record<string, Record<string, Measurement>> };
let trace: Trace | undefined;
let cursor = 0;
let timer: ReturnType<typeof setInterval> | undefined;
let worker: Worker;
let request = 0;
let load_id = 0;
function stop()
{
    clearInterval(timer); timer = undefined;
}
function calculate()
{
    const id = ++request;
    if (!trace) return;
    worker.postMessage({ id, kind: trace.instruction, a: value('rs1'), b: value('rs2'), shift: Number(value('shift')) });
    $('arithmetic').textContent = 'Calculating exact reference…';
}
function diagram(s: Snapshot)
{
    if (!trace) return '';
    const direct = ['fsri_combinational', 'fsri_direct'].includes(trace.variant);
    const sliced = trace.variant === 'fsri_sliced';
    const fsri = trace.instruction === 'fsri';
    const state = Number(s.state);
    const boxes = direct ? [
        ['source registers', 'rs2[31:0] ∥ rs1[31:0]', true], ['funnel shift', '64-bit input → low 32 bits', true], ['PCPI result', 'combinational ready and wr', true]
    ] : sliced ? [
        ['operand latches', 'four 16-bit chunks', state === 1], ['window mux + shift', '32-bit window → 16 bits', state === 1 || state === 2], ['assembly / response', 'two 16-bit halves → rd', state === 4]
    ] : [
        ['operand muxes', fsri ? 'unsigned 32 → signed 33' : 'low 16 signed → signed 33', state === 1],
        ['shared multiplier', 'signed 33 × 33 → 66 bits', [1, 2, 3].includes(state)],
        [fsri ? 'half selection / OR' : 'reduction / response', fsri ? 'high32 then low32 of products' : 'signed 33-bit subtraction', state === 4]
    ];
    return `<svg viewBox="0 0 880 140" role="img" aria-label="Annotated ${escape(trace.variant)} datapath at ${states[state]}">
      <path d="M265 62H315M560 62H610" stroke="currentColor" stroke-width="2"/>
      ${boxes.map(([title, subtitle, active], i) => `<g class="${active ? 'active' : ''}"><rect x="${i * 295 + 8}" y="15" width="260" height="96" rx="12"/><text x="${i * 295 + 138}" y="49" text-anchor="middle">${title}</text><text class="small" x="${i * 295 + 138}" y="77" text-anchor="middle">${subtitle}</text></g>`).join('')}</svg>`;
}
function show_edge()
{
    if (!trace) return;
    const s = trace.snapshots[cursor];
    $('edge-label').textContent = `Snapshot ${s.edge} · ${states[Number(s.state)]}${Number(s.ready) ? ' · response available' : ''}`;
    $('datapath').innerHTML = diagram(s);
    $('timeline').innerHTML = trace.snapshots.map((x, i) => `<button aria-pressed="${i === cursor}" data-edge="${i}">${x.edge}<span>${states[Number(x.state)]}</span>${Number(x.ready) ? 'ready' : Number(x.wait) ? 'wait' : 'idle'}</button>`).join('');
    $('timeline').querySelectorAll<HTMLButtonElement>('button').forEach(button => button.onclick = () => { stop(); cursor = Number(button.dataset.edge); show_edge(); });
    const widths: Record<string, number> = { valid: 1, wait: 1, ready: 1, wr: 1, rd: 32, last_rs1: 32, last_rs2: 32,
        product_value: 32, inverse_value: 16, modulus_value: 32, response_value: 32, multiply_left: 33, multiply_right: 33, multiply_result: 66, numerator: 33, fsri_window: 32, fsri_shifted: 32 };
    $('registers').innerHTML = Object.entries(widths).map(([key, width]) => `<tr><th scope="row">${key}</th><td>${width}</td><td><code>${escape(s[key])}</code></td><td>${signed(BigInt(s[key]), width)}</td></tr>`).join('');
    $('previous').toggleAttribute('disabled', cursor === 0);
    $('step').toggleAttribute('disabled', cursor === trace.snapshots.length - 1);
}
async function load_trace()
{
    const id = ++load_id; stop();
    const entry = catalog.traces.find(x => x.id === value('preset'));
    if (!entry) { trace = undefined; $('trace-status').textContent = 'RTL playback unavailable for this implementation'; return; }
    try
    {
        const loaded = await read<Trace>(entry.file);
        if (id !== load_id) return;
        trace = loaded;
        cursor = 0;
        const source = await fetch(asset(trace.source)).then(r => r.arrayBuffer());
        const hash = [...new Uint8Array(await crypto.subtle.digest('SHA-256', source))].map(x => x.toString(16).padStart(2, '0')).join('');
        if (id !== load_id) return;
        const fresh = hash === trace.source_sha256;
        $('trace-status').textContent = fresh ? `Source hash matches · ${trace.status} · ${trace.tool}` : 'STALE EVIDENCE · source hash mismatch';
        $('trace-status').className = fresh ? 'notice' : 'error';
        $<HTMLInputElement>('rs1').value = trace.rs1;
        $<HTMLInputElement>('rs2').value = trace.rs2;
        $<HTMLInputElement>('shift').value = String(trace.shift);
        $<HTMLInputElement>('shift').disabled = trace.instruction !== 'fsri';
        $('trace-inputs').textContent = `Recorded inputs: rs1 = ${trace.rs1}, rs2 = ${trace.rs2}, shift = ${trace.shift}. Editing the calculator does not alter this recording.`;
        const encoding = trace.instruction === 'fsri' ? 'mask 0xc000707f · match 0x0000200b · shamt = insn[29:25]' : `mask 0xfe00707f · match ${trace.instruction === 'fqmul' ? '0x0000000b' : '0x0000100b'}`;
        $('encoding').innerHTML = `<p><strong>Project-specific custom-0 encoding</strong> · ${encoding}<br>Recorded word <code>${trace.encoding}</code> · rd = x7 · rs1 = x5 · ${trace.instruction === 'red32' ? 'rs2 = x0' : 'rs2 = x6'}</p><p>Active parameters <code>${escape(JSON.stringify(trace.parameters))}</code></p>`;
        $('trace-links').innerHTML = `${link(entry.file, 'Download trace JSON')} · ${link(trace.vcd, 'Download VCD')} · ${link(trace.source, 'Matching RTL')} · ${link(`${trace.instruction}.h`, 'C wrapper')} · ${link('catalog.json', 'Build manifest')}`;
        $('latency').textContent = `${trace.sampling}. First ready: snapshot ${trace.response_edge}. ${trace.latency}. Direct ready at snapshot 0 can be sampled at the next rising edge; sequential request capture is edge 1. Previous replays stored history.`;
        $('implementation').textContent = trace.variant === 'fsri_combinational' ? `Original direct RTL recovered from ${trace.source_revision}. This new trace does not reproduce historical area or full-operation results.` : 'Current project RTL with the selected feature enabled. Other custom features are disabled; the shared RV32M multiplier remains present.';
        show_edge(); calculate();
        load_schematic();
    }
    catch (error) { $('trace-status').textContent = String(error); }
}
function select_variant()
{
    const selected = value('variant');
    $('preset').innerHTML = catalog.traces.filter(x => x.variant === selected).map((x, i) => `<option value="${x.id}">Recorded example ${i + 1}</option>`).join('');
    void load_trace();
}
function budget(): Budget
{
    return { lut: Number(value('lut')), ff: Number(value('ff')), loss: Number(value('loss')), dsp: Number(value('dsp')), bram: Number(value('bram')), seeds: $<HTMLInputElement>('seeds').checked };
}
function results()
{
    const level = value('level'), operation = value('operation'), mode = value('mode');
    const b = budget();
    if (Object.entries(b).some(([k, v]) => k !== 'seeds' && (!Number.isFinite(v) || Number(v) < 0)))
    { $('eligibility').textContent = 'Enter nonnegative finite budget limits'; return; }
    const levels = summary.levels[level];
    const cycles = (key: string): number | null => operation === 'total' ? (typeof levels[key] === 'number' ? levels[key] : null) :
        measurements.levels[level]?.[key]?.cycles[operation] ?? levels.operation_cycles[key]?.[operation] ?? null;
    const baseline = cycles('software')!;
    const points: { key: string; lut: number; cycles: number; eligible: boolean }[] = [];
    const rows = Object.entries(names).filter(([key]) => key in levels).map(([key, title]) =>
    {
        const h = summary.hardware[key] ?? summary.hardware.baseline;
        const custom = key !== 'software' && key !== 'portable';
        const reasons = failures(h, summary.hardware.baseline, b);
        const count = cycles(key);
        const frequency = h.median_fmax_mhz;
        const amount = count === null ? 'missing' : mode === 'cycles' ? count.toLocaleString() : frequency ? `${(count / (frequency * 1000)).toFixed(2)} ms` : 'missing Fmax';
        const evidence = measurements.levels[level]?.[key];
        const instructions = evidence ? operation === 'total' ? Object.values(evidence.instructions).reduce((a, x) => a + x, 0) : evidence.instructions[operation] : null;
        const seeds = h.fmax_by_seed_mhz?.map((x, i) => `${i + 1}: ${x ?? 'missing'}`).join(' · ') ?? 'values missing · historical summary reports all five pass';
        if (count !== null && h.lut4 !== null) points.push({ key, lut: h.lut4, cycles: mode === 'cycles' ? count : count / (frequency! * 1000), eligible: reasons.length === 0 });
        const delta = (field: 'lut4' | 'flip_flops' | 'dsp' | 'bram') => h[field] === null ? 'missing' : `${h[field]} (${h[field]! - summary.hardware.baseline[field]! >= 0 ? '+' : ''}${h[field]! - summary.hardware.baseline[field]!})`;
        return `<tr><th scope="row">${title}<small>${evidence ? 'raw cycle evidence available' : 'repository-reported historical result'}</small></th><td>${amount}<small>${count === null ? '' : `${((1 - count / baseline) * 100).toFixed(2)}% fewer cycles`}</small></td><td>${instructions?.toLocaleString() ?? 'missing'}</td><td>${delta('lut4')}<br>${delta('flip_flops')}</td><td>${delta('dsp')} / ${delta('bram')}</td><td>${frequency ?? 'missing'} MHz<small>${seeds}</small></td><td>${custom ? reasons.length ? reasons.map(escape).join('<br>') : 'eligible under selected limits' : 'comparison baseline'}<small>${custom && key !== 'fqmul' ? 'raw synthesis absent' : key === 'fqmul' ? 'raw synthesis available' : 'project multiplier baseline'}</small></td><td>${link('summary.json', 'Summary')}${evidence ? `<br><a href="${source_link(evidence.source)}" target="_blank" rel="noopener">Raw cycles</a>` : '<br>Raw cycles missing'}</td></tr>`;
    });
    $('result-rows').innerHTML = rows.join('');
    const eligible = points.filter(x => !['software', 'portable'].includes(x.key) && x.eligible);
    $('eligibility').textContent = eligible.length ? `Eligible under selected limits: ${eligible.map(x => names[x.key]).join(', ')}. Historical claims remain historical.` : 'No eligible custom implementation';
    $('mode-note').textContent = mode === 'cycles' ? 'Common-clock comparison in processor cycles. A common clock does not establish that every routing seed can meet it.' : 'Modeled estimate: cycles / each design’s repository-reported median Fmax. Frequencies are shown per row. This is not measured board throughput and the median does not guarantee all-seed timing.';
    const max = Math.max(...points.map(p => p.cycles), 1), min_lut = 3500, max_lut = 4100;
    $('pareto').innerHTML = `<svg viewBox="0 0 800 300" role="img" aria-label="Interactive Pareto comparison of core LUT4 versus selected cycle or modeled time cost"><path d="M60 20V250H760" fill="none" stroke="currentColor"/><text x="320" y="290">Core LUT4 → lower is better</text><text x="70" y="18">${mode === 'cycles' ? 'Cycles' : 'Modeled milliseconds'} ↑</text>${points.map(p => {
        const x = 65 + (p.lut - min_lut) / (max_lut - min_lut) * 650;
        const y = 245 - p.cycles / max * 210;
        const dominated = points.some(q => q !== p && q.lut <= p.lut && q.cycles <= p.cycles && (q.lut < p.lut || q.cycles < p.cycles));
        return `<g tabindex="0" role="button" aria-label="${escape(names[p.key])} ${p.lut} LUT4 ${p.cycles.toFixed(0)} ${dominated ? 'dominated' : 'Pareto frontier'}" data-point="${p.key}"><title>${escape(names[p.key])}: ${p.lut} LUT4 · ${p.cycles.toFixed(2)} · ${dominated ? 'dominated' : 'Pareto frontier'}</title><circle cx="${x}" cy="${y}" r="${dominated ? 6 : 9}" class="${p.eligible ? 'eligible' : 'ineligible'}"/><text x="${x + 12}" y="${y + 5}" class="small">${escape(p.key.replace('fsri_', ''))}</text></g>`;
    }).join('')}</svg>`;
    $('pareto').querySelectorAll<SVGElement>('[data-point]').forEach(point => {
        const select = () => { $('point-detail').textContent = point.getAttribute('aria-label'); };
        point.onclick = select; point.onkeydown = event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); select(); } };
    });
}
async function load_schematic()
{
    if (!trace) return;
    const variant = trace.variant;
    try
    {
        const manifest = await read<{ variants: Record<string, { svg: string; stage: string; source_sha256: string }> }>('schematics.json');
        if (variant !== trace?.variant) return;
        const entry = manifest.variants[variant];
        if (!entry) throw new Error('not generated');
        $('schematic').innerHTML = `<p>${escape(entry.stage)} · ${entry.source_sha256 === trace.source_sha256 ? 'source hash matches' : 'STALE SOURCE HASH'}. PCPI only; not mapped core resources or a floorplan.</p>${link(entry.svg, 'Open full schematic')} · ${link('schematics.json', 'Generation manifest')}<div class="schematic-scroll"><img src="${asset(entry.svg)}" alt="Generated ${escape(variant)} RTLIL schematic"></div>`;
    }
    catch { $('schematic').textContent = 'Generated schematic unavailable. Run the optional offline schematic exporter with Yosys and netlistsvg.'; }
}
async function main()
{
    [catalog, summary, measurements] = await Promise.all([read<Catalog>('catalog.json'), read<Summary>('summary.json'), read<typeof measurements>('measurements.json')]);
    $('app').innerHTML = `<header><a class="brand" href="#main">pqc / poly bench</a><nav aria-label="Workbench sections"><a href="#instruction">Instruction</a><a href="#measurements">Measurements</a><a href="#planner">Planner</a><a href="#guide">Guide</a></nav></header>
    <main id="main"><div class="intro"><p class="eyebrow">PicoRV32 · ML-KEM · hardware / software co-design</p><h1>Follow the instruction.<br>Inspect the tradeoffs.</h1><p>Inspect real RTL transactions, exact finite-width arithmetic, and the evidence behind the results.</p><span class="badge">Recorded RTL playback · native Verilator</span><p class="muted">Live WASM model unavailable: Emscripten is not installed. The arithmetic calculator accepts arbitrary operands; hardware playback uses fixed recorded inputs.</p></div>
    <section id="instruction"><div class="section-head"><h2>01 / Instruction workbench</h2><span>source → request → response</span></div><div class="controls"><label>Instruction / implementation<select id="variant">${options(Object.fromEntries(Object.entries(names).filter(([k]) => catalog.traces.some(t => t.variant === k))))}</select></label><label>RTL recording<select id="preset"></select></label></div><p id="trace-status" role="status"></p><p id="implementation"></p><div id="encoding"></div>
    <div class="split"><article><h3>Exact arithmetic reference</h3><p>Independent BigInt calculator · no generated RTL states</p><div class="controls"><label>rs1 · hex or decimal<input id="rs1" spellcheck="false" maxlength="12"></label><label>rs2 · hex or decimal<input id="rs2" spellcheck="false" maxlength="12"></label><label>FSRI shift <input id="shift" type="number" min="0" max="31" value="0"></label><button id="calculate">Calculate</button></div><div id="operand-values"></div><pre id="arithmetic" role="status" aria-live="polite"></pre><p id="arithmetic-notes"></p></article>
    <article><h3>Recorded transaction</h3><p id="trace-inputs"></p><div class="controls"><button id="reset">Reset playback</button><button id="previous">Previous</button><button id="step">Single-step</button><button id="run">Run recording</button><button id="cancel">Cancel</button></div><h4 id="edge-label" aria-live="polite"></h4><div id="timeline" aria-label="Recorded clock snapshots"></div><p id="latency" class="muted"></p><p id="trace-links" class="links"></p></article></div>
    <h3>Annotated datapath</h3><p>A pedagogical explanation of this implementation. Highlights follow the recorded state; this is not a generated netlist.</p><div id="datapath"></div><details><summary>Signals and intermediate registers at this snapshot</summary><p>Raw bit patterns and signed interpretations. Inactive registers can retain old values; rd is meaningful only when wr and ready are asserted.</p><div class="table-scroll"><table><thead><tr><th>Signal</th><th>Width</th><th>Raw bits</th><th>Signed value</th></tr></thead><tbody id="registers"></tbody></table></div></details><details><summary>Generated hardware schematic</summary><div id="schematic"></div></details></section>
    <section id="measurements"><div class="section-head"><h2>02 / Measured trade-offs</h2><span>historical evidence · adjustable limits</span></div><div class="controls"><label>ML-KEM level<select id="level">${options({'512': 'ML-KEM-512', '768': 'ML-KEM-768', '1024': 'ML-KEM-1024'})}</select></label><label>Operation<select id="operation">${options({ total: 'Sum of operation medians', keygen: 'Key generation', encapsulation: 'Encapsulation', decapsulation: 'Decapsulation' })}</select></label><label>Comparison<select id="mode">${options({ cycles: 'Common-clock cycles', frequency: 'Modeled cycles / median Fmax' })}</select></label></div>
    <fieldset><legend>Original hardware budget · changes only reclassify existing measurements</legend><div class="controls">${[['lut', 'LUT4 increase %', 5], ['ff', 'Flip-flop increase %', 5], ['loss', 'Median Fmax loss %', 2], ['dsp', 'Extra DSP', 0], ['bram', 'Extra BRAM', 0]].map(([id, label, n]) => `<label>${label}<input id="${id}" type="number" min="0" max="100" step="1" value="${n}"></label>`).join('')}<label class="check"><input id="seeds" type="checkbox" checked>All five seeds meet 50 MHz</label><button id="original">Restore original budget</button></div></fieldset>
    <p id="eligibility" class="notice" role="status"></p><p id="mode-note"></p><p>Totals are sums of per-operation medians, not sampled end-to-end medians. NTT microbenchmarks are separate workloads. No combined FQMUL × FSRI speedup is inferred.</p><div class="table-scroll"><table class="results"><thead><tr><th>Implementation / evidence</th><th>Cycles or modeled time</th><th>Retired instructions</th><th>LUT4 / FF (delta)</th><th>DSP / BRAM (delta)</th><th>Median Fmax / seeds</th><th>Budget failures</th><th>Source</th></tr></thead><tbody id="result-rows"></tbody></table></div><div id="pareto"></div><p id="point-detail" aria-live="polite">Select a plotted point to inspect its cost. Larger circles mark the two-axis Pareto frontier; eligibility applies the full budget.</p>
    <details><summary>Measurement scope and evidence levels</summary><p>Cycle counts came from full CPU RTL simulation. Synthesis measures the ECP5 core, not a complete physical board. Zero core BRAM does not mean ML-KEM needs no memory. The comparison baseline uses the project multiplier with STOCK_MUL=0, not the stock PicoRV32 fast multiplier.</p><p>Repository-reported historical results preserve checked-in summaries. Raw evidence is available for software and FQMUL more fully than RED32 or FSRI. Local reproduction here covers instruction traces only. Modeled estimates are calculations with explicit assumptions. Failed measurements with zero resource or frequency fields are treated as missing, with original evidence retained. Historical verified flags do not mean this work reran those checks.</p><p>Direct FSRI lacks per-seed values and operation breakdowns in the summary. Its all-seed claim is reported, not reconstructed. Shared multiplier logic means PCPI schematic costs cannot be added as independent instruction costs.</p>${link('measurements.json', 'Extracted raw evidence and original synthesis records')}</details></section>
    <section id="planner"><h2>03 / Compiler schedule walkthrough</h2><div id="planner-content">Planner export unavailable. Generate schedules with the optional C++ exporter.</div></section>
    <section id="experiments"></section><section id="guide"><h2>05 / Follow the implementation</h2><div class="split"><article><h3>Three live demo actions</h3><ol><li>Select FQMUL and single-step the three uses of the shared multiplier. Inspect the 66-bit product and 33-bit subtraction.</li><li>Select both FSRI implementations with the same shift. Observe combinational ready versus a captured multi-edge transaction.</li><li>Compare FSRI cycle savings, switch to the frequency model, then restore the original budget. No custom implementation qualifies.</li></ol></article><article><h3>What is actually established</h3><p>The calculator is exact finite-width arithmetic. The recordings are new native Verilator runs. Performance and area figures remain repository-reported historical results, with raw evidence linked where available.</p><p>Existing FSRI formal tasks are bounded: PCPI depth 8 and RVFI depth 22. They do not establish an unbounded proof or physical side-channel resistance. Local PCPI reruns are listed in the experimental results; the RVFI task was not rerun.</p><p>The compiler uses ML-KEM’s seven-layer incomplete NTT and Montgomery conventions. Fusion changes memory reuse; lazy reduction is constrained by signed storage bounds. Standalone kernel winners need not win complete operations.</p></article></div><p>${link('catalog.json', 'Source and build provenance')} · <a href="https://github.com/ishanrk/embedded_mlkem_bench">Repository</a> · <a href="https://eprint.iacr.org/2020/049">Finite-field ISA prior art</a></p></section></main><footer>pqc_poly_bench · evidence before performance claims</footer>`;
    worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = event => {
        if (event.data.id !== request) return;
        if (event.data.error) { $('arithmetic').textContent = event.data.error; $('arithmetic').className = 'error'; return; }
        $('arithmetic').className = '';
        const result = event.data.result as Record<string, string>;
        if (trace?.instruction === 'fsri')
        {
            const keep = trace.variant === 'fsri_multiplier_reuse' ? ['rd', 'joined', 'factor', 'lower_product', 'upper_product'] : trace.variant === 'fsri_sliced' ? ['rd', 'joined', 'coarse', 'residual', 'low_window', 'high_window', 'low_half', 'high_half'] : ['rd', 'joined'];
            for (const key of Object.keys(result)) if (!keep.includes(key)) delete result[key];
        }
        $('arithmetic').textContent = Object.entries(result).map(([k, v]) => `${k.padEnd(18)} ${v}${k === 'rd' ? `  (${hex(BigInt(v))}, signed ${signed(BigInt(v), 32)})` : ''}`).join('\n');
        $('operand-values').textContent = ['rs1', 'rs2'].map(k => { const v = operand(value(k)); return `${k}: ${hex(v)} · signed ${signed(v, 32)} · unsigned ${v}`; }).join(' | ');
        $('arithmetic-notes').textContent = trace?.variant === 'fsri_sliced' ? 'Sliced FSRI uses four 16-bit chunks and the coarse shift bit to select two adjacent-chunk windows. The shared 32-bit window shifts by 0–15 and retains only low16; two halves assemble the same 32-bit result. No multiplier product contributes to FSRI in this mode. See the experiment below for the chunk algebra.' : trace?.instruction === 'fsri' ? 'FSRI = low32(({rs2, rs1} >> shamt)). Reuse: high32(rs1 × 2^(32−s)) OR low32(rs2 × 2^(32−s)); at s=0 return rs1. Keccak ROL64: n=0 copies; n=32 swaps halves using shift zero; n<32 swaps operands with shift 32−n; n>32 uses shift 64−n. Both halves use swapped source order.' : 'FQMUL uses only signed low16 of each source; RED32 uses signed32 rs1 and ignores rs2. Product is 32 bits; low16 × 62209 is truncated to signed16 u; u × 3329 is 32 bits. Subtraction is signed33, followed by an exact 16-bit shift and sign extension to 32. The machine result need not be canonical; residue is shown separately. Montgomery multiplication removes one factor of R = 65536.';
        if (trace && trace.instruction !== 'fsri')
            $('arithmetic-notes').textContent = `${trace.instruction === 'fqmul' ? 't = sign16(low16(rs1)) × sign16(low16(rs2))' : 't = sign32(rs1)'}; u = sign16(low16(t) × 62209 mod 65536); rd = (t − u × 3329) / 65536. ${$('arithmetic-notes').textContent}`;
    };
    worker.onerror = () => { $('arithmetic').textContent = 'Arithmetic worker unavailable; reload to retry'; };
    $('variant').onchange = select_variant; $('preset').onchange = () => void load_trace();
    $('calculate').onclick = calculate;
    for (const key of ['rs1', 'rs2', 'shift']) $(key).oninput = calculate;
    $('reset').onclick = () => { stop(); cursor = 0; show_edge(); };
    $('previous').onclick = () => { stop(); cursor = Math.max(0, cursor - 1); show_edge(); };
    $('step').onclick = () => { stop(); if (trace) cursor = Math.min(trace.snapshots.length - 1, cursor + 1); show_edge(); };
    $('run').onclick = () => { stop(); if (!trace) return; cursor = 0; show_edge(); timer = setInterval(() => { if (!trace || cursor >= trace.snapshots.length - 1) { stop(); return; } ++cursor; show_edge(); }, 650); };
    $('cancel').onclick = stop;
    for (const key of ['level', 'operation', 'mode', 'lut', 'ff', 'loss', 'dsp', 'bram', 'seeds']) $(key).oninput = results;
    $('original').onclick = () => { for (const k of ['lut', 'ff', 'loss', 'dsp', 'bram'] as const) $<HTMLInputElement>(k).value = String(original_budget[k]); $<HTMLInputElement>('seeds').checked = true; results(); };
    select_variant(); results();
    void import('./experiments').then(m => m.show_experiments(asset, $('experiments')));
    void import('./planner').then(m => m.show_planner(asset, $('planner-content')));
}
void main().catch(error => { $('app').innerHTML = `<main><h1>Evidence unavailable</h1><p role="alert">${escape(error)}</p><p>Generate the bundled evidence and serve the built app over HTTP, then reload.</p><button onclick="location.reload()">Retry</button></main>`; });
