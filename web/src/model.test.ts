import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { arithmetic, operand, failures, original_budget, normalize_seed } from './model.ts';

// checks fixed width arithmetic signs and accepted operand text
test('finite width arithmetic and operand validation', () => {
    assert.equal(operand('-2147483648'), 0x80000000n);
    for (const value of ['4294967296', '-2147483649', '1e3', '0x', '', '1.5']) assert.throws(() => operand(value));
    // unusual upper halves confirm FQMUL reads only the signed low halves
    assert.equal(arithmetic('fqmul', 0xdead8000n, 0xbeef0680n, 0).rd, arithmetic('fqmul', 0x8000n, 0x680n, 0).rd);
    assert.equal(arithmetic('red32', 0x7fffffffn, 0n, 0).result, 32599n);
    for (let shift = 0; shift < 32; shift++) {
        const a = 0x89abcdefn, b = 0x12345678n;
        assert.equal(arithmetic('fsri', a, b, shift).rd, ((b << 32n | a) >> BigInt(shift)) & 0xffffffffn);
    }
    assert.throws(() => arithmetic('fsri', 0n, 0n, 32));
});
test('all native recordings agree with the independent bigint reference', () => {
    // checks every saved Verilator trace against the arithmetic model
    const catalog = JSON.parse(readFileSync('public/evidence/catalog.json', 'utf8'));
    for (const entry of catalog.traces) {
        const trace = JSON.parse(readFileSync(`public/evidence/${entry.file}`, 'utf8'));
        const expected = arithmetic(trace.instruction, BigInt(trace.rs1), BigInt(trace.rs2), trace.shift).rd;
        const response = trace.snapshots.find((s: Record<string, string>) => BigInt(s.ready) === 1n);
        assert.equal(BigInt(response.rd), expected, entry.id);
        assert.equal(response.edge, trace.response_edge);
    }
});
test('original budget rejects every reported custom variant and relaxed budget reclassifies', () => {
    // checks recorded results under original and changed hardware limits
    const { hardware } = JSON.parse(readFileSync('public/evidence/summary.json', 'utf8'));
    for (const [key, h] of Object.entries(hardware)) {
        if (key !== 'baseline') assert.ok(failures(h as never, hardware.baseline, original_budget).length > 0, key);
    }
    assert.deepEqual(failures(hardware.fsri_combinational, hardware.baseline, { ...original_budget, lut: 13, loss: 3 }), []);
    assert.ok(failures(hardware.fsri_multiplier_reuse, hardware.baseline, { ...original_budget, lut: 20, ff: 20, loss: 30 }).some(x => x.includes('3/5')));
});
test('failed parser zeros remain missing with original evidence retained', () => {
    // failed routing must appear as missing rather than zero area or frequency
    const raw = { lut4: 0, maximum_frequency_mhz: 0 };
    const normalized = normalize_seed(raw);
    assert.equal(normalized.lut4, null);
    assert.equal(normalized.frequency, null);
    assert.equal(normalized.original, raw);
    assert.equal(normalized.status, 'incomplete/failed run');
});
