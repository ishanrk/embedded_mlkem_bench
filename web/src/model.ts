export type Instruction = 'fqmul' | 'red32' | 'fsri';
export type Variant = 'baseline' | Instruction;

export type Measurement = {
    keygen_cycles: number | null;
    encapsulation_cycles: number | null;
    decapsulation_cycles: number | null;
    total_cycles: number | null;
    percent_change_vs_baseline: number | null;
    verified: boolean;
};

export type Hardware = {
    lut4: number | null;
    flip_flops: number | null;
    dsp: number | null;
    bram: number | null;
    fmax_by_seed_mhz: number[];
    median_fmax_mhz: number | null;
    meets_50mhz_by_seed: boolean[];
    fmax_change_vs_baseline_percent: number | null;
};

export type Summary = {
    schema: string;
    status: string;
    platform: string;
    pins: { mlkem_native: string; picorv32: string };
    baseline: string;
    measurements: Record<Variant, Record<string, Measurement>>;
    hardware: Record<Variant, Hardware>;
};

export const signed = (value: bigint, width: number) => BigInt.asIntN(width, value);
export const unsigned = (value: bigint, width = 32) => BigInt.asUintN(width, value);

export function operand(text: string): bigint
{
    const input = text.trim();
    if (!/^(?:0x[0-9a-f]{1,8}|-?[0-9]{1,10})$/i.test(input))
        throw new Error('enter hexadecimal or a signed decimal value');
    const value = BigInt(input);
    if (value < -2147483648n || value > 4294967295n)
        throw new Error('operand is outside the register range');
    return unsigned(value);
}

export function arithmetic(kind: Instruction, first: bigint, second: bigint, shift: number)
{
    if (!Number.isInteger(shift) || shift < 0 || shift > 31)
        throw new Error('shift must be between 0 and 31');
    first = unsigned(first);
    second = unsigned(second);
    if (kind === 'fsri')
    {
        const joined = second << 32n | first;
        return { joined, result: unsigned(joined >> BigInt(shift)) };
    }
    const product = kind === 'fqmul'
        ? signed(first, 16) * signed(second, 16)
        : signed(first, 32);
    const inverse = signed(unsigned(product, 16) * 62209n, 16);
    const modulusProduct = inverse * 3329n;
    const numerator = product - modulusProduct;
    return {
        product,
        inverse,
        modulusProduct,
        numerator,
        result: unsigned(numerator / 65536n),
    };
}

export function display(value: number | null, suffix = ''): string
{
    return value === null ? 'pending rerun' : `${value.toLocaleString()}${suffix}`;
}
