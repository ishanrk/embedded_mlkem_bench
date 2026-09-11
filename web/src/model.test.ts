import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { arithmetic, display, operand } from './model.ts';

test('instruction arithmetic uses the intended source widths', () => {
    assert.equal(operand('-2147483648'), 0x80000000n);
    for (const value of ['4294967296', '-2147483649', '1e3', '0x', ''])
        assert.throws(() => operand(value));
    assert.equal(
        arithmetic('fqmul', 0xdead8000n, 0xbeef0680n, 0).result,
        arithmetic('fqmul', 0x8000n, 0x0680n, 0).result,
    );
    assert.equal(arithmetic('red32', 0x7fffffffn, 9n, 0).result, 32599n);
    for (let shift = 0; shift < 32; shift++)
    {
        const first = 0x89abcdefn;
        const second = 0x12345678n;
        const expected = ((second << 32n | first) >> BigInt(shift)) & 0xffffffffn;
        assert.equal(arithmetic('fsri', first, second, shift).result, expected);
    }
});

test('summary contains only the four current variants', () => {
    const summary = JSON.parse(readFileSync('public/evidence/summary.json', 'utf8'));
    assert.deepEqual(Object.keys(summary.measurements), ['baseline', 'fqmul', 'red32', 'fsri']);
    assert.deepEqual(Object.keys(summary.hardware), ['baseline', 'fqmul', 'red32', 'fsri']);
    assert.equal(display(null), 'pending rerun');
});

test('catalog sources and generated artifacts are present', () => {
    const catalog = JSON.parse(readFileSync('public/evidence/catalog.json', 'utf8'));
    assert.equal(catalog.schema, 'pqc-poly-bench/workbench-v2');
    for (const name of Object.keys(catalog.artifacts))
        assert.doesNotThrow(() => readFileSync(`public/evidence/${name}`));
});
