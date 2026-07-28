type Event = { left: number; right: number; zeta_index: number; length: number; fused: boolean };
type Candidate = { id: string; plan: Record<string, string>; forward_bound: number; inverse_lazy_bound: number;
    accumulator_bound: number; scratch_bytes: number; caller_workspace_bytes: number;
    memory_checks: { scratch_limit: number; caller_workspace_limit: number; legal: boolean; rejections: string[]; checker: string[] }[] };
export async function show_planner(asset: (name: string) => string, container: HTMLElement)
{
    try
    {
        const response = await fetch(asset('planner.json'));
        if (!response.ok) throw new Error('export unavailable');
        const data = await response.json() as { candidates: Candidate[]; operation_order: Record<string, Event[]>; inverse_records: unknown[] };
        const select = (key: string, label: string, values: string[]) => `<label>${label}<select id="plan-${key}">${values.map(v => `<option>${v}</option>`).join('')}</select></label>`;
        const fields = { level: 'ML-KEM level', forward: 'Forward traversal', inverse: 'Inverse traversal', inverse_reduction: 'Inverse sum reduction', basemul: 'Base multiplication', instruction: 'Instruction' };
        container.innerHTML = `<p>144 plans from the C++ compiler. Memory controls select analyzed requests; legality and rejection reasons come from the existing checker.</p><div class="controls">${Object.entries(fields).map(([key, label]) => select(key, label, [...new Set(data.candidates.map(c => c.plan[key]))])).join('')}${select('scratch', 'Scratch limit · bytes', ['1024', '768', '512', '0'])}${select('workspace', 'Caller workspace limit · bytes', ['4608', '3584', '2560', '0'])}</div><p id="plan-status" class="notice" role="status"></p><pre id="plan-bounds"></pre><div class="controls"><label>Forward butterfly order · 0–895<input id="plan-index" type="range" min="0" max="895" value="0"></label><button id="plan-next">Next butterfly</button></div><div id="plan-event"></div><details><summary>Inverse logical records</summary><p>These are checker records in logical stage order. They are not the execution order of the fused inverse helper.</p><pre id="plan-inverse-records"></pre></details><p>Forward order was recorded from generated C with zero inputs. Only schedule indices are displayed; no live coefficient execution is implied. Bounds are conservative aggregate bounds from the planner and checker.</p><div class="split"><article><h3>Why fusion changes reuse</h3><p>Stage traversal loads two coefficients and stores two results for each butterfly. Two-layer fusion loads four coefficients, retains intermediate values in local variables, then writes four outputs. The four arithmetic butterflies still run; the intermediate array round trip is removed.</p></article><article><h3>Why reduction cannot wait forever</h3><p>Lazy sums grow before reduction. The checker propagates conservative intervals and checks signed16 coefficient storage, signed32 accumulation, and Montgomery input range. The current inverse pair delay is checked; this interface does not claim arbitrary delays are safe. Full-operation selection also includes cache generation and complete ML-KEM work, so the fastest standalone NTT need not select the winning plan.</p></article></div><p><a href="${asset('planner.json')}" target="_blank" rel="noopener">Download analyzed plans, observed order and provenance</a></p>`;
        const get = (key: string) => container.querySelector<HTMLInputElement | HTMLSelectElement>(`#plan-${key}`)!;
        const update = () => {
            const candidate = data.candidates.find(c => Object.keys(fields).every(key => c.plan[key] === get(key).value))!;
            const check = candidate.memory_checks.find(c => c.scratch_limit === Number(get('scratch').value) && c.caller_workspace_limit === Number(get('workspace').value))!;
            container.querySelector('#plan-status')!.textContent = `${candidate.id} · ${check.legal ? 'legal' : `rejected: ${check.rejections.join(', ')}`} · checker: ${check.checker.length ? check.checker.join(', ') : 'pass'}`;
            container.querySelector('#plan-bounds')!.textContent = `forward bound       ${candidate.forward_bound}\ninverse lazy bound  ${candidate.inverse_lazy_bound}\naccumulator bound   ${candidate.accumulator_bound}\nscratch bytes       ${candidate.scratch_bytes}\ncaller workspace    ${candidate.caller_workspace_bytes}`;
            const index = Number(get('index').value);
            const event = data.operation_order[candidate.plan.forward][index];
            const layer = 8 - Math.log2(event.length);
            container.querySelector('#plan-event')!.innerHTML = `<h3>Butterfly ${index} · layer ${layer} · zeta[${event.zeta_index}]</h3><p>Logical coefficient pair r[${event.left}], r[${event.right}] · distance ${event.length} coefficients = ${event.length * 2} bytes. These are separate memory locations, not one free packed load.</p><pre>t = fqmul(right, zeta[${event.zeta_index}])\nleft_out = left + t\nright_out = left - t</pre><p>${event.fused ? 'Fused group: inputs and intermediate values are held in four C locals; stores happen after the pair of layers.' : 'Stage helper: two coefficient loads and two stores for this butterfly.'} Each multiplication uses Montgomery reduction; zeta is stored in Montgomery representation. Signed16 output storage is justified by the existing seven-layer interval analysis; the displayed aggregate forward bound is ${candidate.forward_bound}.</p>`;
            container.querySelector('#plan-inverse-records')!.textContent = JSON.stringify(data.inverse_records, null, 2);
        };
        container.querySelectorAll('select,input').forEach(control => control.addEventListener('input', update));
        container.querySelector('#plan-next')!.addEventListener('click', () => { get('index').value = String((Number(get('index').value) + 1) % 896); update(); });
        update();
    }
    catch { container.textContent = 'Planner export unavailable. Run python3 scripts/workbench_planner.py with a C++20 compiler, then rebuild the app.'; }
}
