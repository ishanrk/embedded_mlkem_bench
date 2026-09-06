export type Instruction = 'fqmul' | 'red32' | 'fsri';
export type Hardware = {
    lut4: number | null; flip_flops: number | null; dsp: number | null; bram: number | null;
    median_fmax_mhz: number | null; fmax_by_seed_mhz?: (number | null)[];
    all_seeds_meet_50mhz?: boolean;
};
export type Budget = { lut: number; ff: number; loss: number; dsp: number; bram: number; seeds: boolean };
export const original_budget: Budget = { lut: 5, ff: 5, loss: 2, dsp: 0, bram: 0, seeds: true };
export const signed = (value: bigint, width: number) => BigInt.asIntN(width, value);
export const unsigned = (value: bigint, width = 32) => BigInt.asUintN(width, value);
export const hex = (value: bigint, width = 32) => `0x${unsigned(value, width).toString(16).padStart(Math.ceil(width / 4), '0')}`;
export function operand(text: string): bigint
{
    if (!/^(?:0x[0-9a-f]{1,8}|-?[0-9]{1,10})$/i.test(text.trim()))
        throw new Error('Enter 1–8 hexadecimal digits after 0x or a signed/unsigned 32-bit decimal integer');
    const value = BigInt(text.trim());
    if (value < -2147483648n || value > 4294967295n) throw new Error('Operand is outside the 32-bit range');
    return unsigned(value);
}
export function arithmetic(kind: Instruction, a: bigint, b: bigint, shift: number)
{
    if (!Number.isInteger(shift) || shift < 0 || shift > 31) throw new Error('Shift must be 0–31');
    a = unsigned(a); b = unsigned(b);
    if (kind === 'fsri')
    {
        const joined = (b << 32n) | a;
        return { rd: unsigned(joined >> BigInt(shift)), joined,
            factor: shift === 0 ? 0n : 1n << BigInt(32 - shift),
            lower_product: shift === 0 ? 0n : a * (1n << BigInt(32 - shift)),
            upper_product: shift === 0 ? 0n : b * (1n << BigInt(32 - shift)) };
    }
    const t = kind === 'red32' ? signed(a, 32) : signed(a, 16) * signed(b, 16);
    const low = unsigned(t, 16);
    const u = signed(low * 62209n, 16);
    const numerator = t - u * 3329n;
    const result = numerator / 65536n;
    return { rd: unsigned(result), a: signed(a, 16), b: signed(b, 16), t, low, u,
        modulus_product: u * 3329n, numerator, result, residue: (result % 3329n + 3329n) % 3329n };
}
export function failures(h: Hardware, base: Hardware, budget: Budget): string[]
{
    const out: string[] = [];
    for (const [field, limit, label] of [['lut4', budget.lut, 'LUT4'], ['flip_flops', budget.ff, 'flip-flops']] as const)
    {
        if (h[field] === null || base[field] === null || h[field]! <= 0 || base[field]! <= 0) out.push(`${label} evidence missing`);
        else if ((h[field]! / base[field]! - 1) * 100 > limit) out.push(`${label} +${((h[field]! / base[field]! - 1) * 100).toFixed(2)}% > ${limit}%`);
    }
    if (!h.median_fmax_mhz || !base.median_fmax_mhz) out.push('Fmax evidence missing');
    else if ((1 - h.median_fmax_mhz / base.median_fmax_mhz) * 100 > budget.loss)
        out.push(`Fmax loss ${((1 - h.median_fmax_mhz / base.median_fmax_mhz) * 100).toFixed(2)}% > ${budget.loss}%`);
    for (const [field, limit] of [['dsp', budget.dsp], ['bram', budget.bram]] as const)
    {
        if (h[field] === null || base[field] === null) out.push(`${field} evidence missing`);
        else if (h[field]! - base[field]! > limit) out.push(`${field} increase > ${limit}`);
    }
    if (budget.seeds)
    {
        const seeds = h.fmax_by_seed_mhz;
        if (seeds && seeds.length === 5 && seeds.every(x => x !== null && x > 0))
        {
            if (seeds.some(x => x! < 50)) out.push(`${seeds.filter(x => x! >= 50).length}/5 seeds meet 50 MHz`);
        }
        else if (h.all_seeds_meet_50mhz !== true) out.push('five passing seeds unconfirmed');
    }
    return out;
}
export function normalize_seed(seed: Record<string, number>)
{
    const failed = seed.maximum_frequency_mhz <= 0 || seed.lut4 <= 0;
    return { original: seed, status: failed ? 'incomplete/failed run' : 'raw evidence available',
        lut4: failed ? null : seed.lut4, frequency: failed ? null : seed.maximum_frequency_mhz };
}
