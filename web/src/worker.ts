import { arithmetic, operand, type Instruction } from './model';
// calculates instruction results outside the browser interface event loop
self.onmessage = (event: MessageEvent<{ id: number; kind: Instruction; a: string; b: string; shift: number }>) =>
{
    try
    {
        const { id, kind, a, b, shift } = event.data;
        const result = arithmetic(kind, operand(a), operand(b), shift);
        // sends decimal strings because older browsers cannot clone BigInt values
        self.postMessage({ id, result: Object.fromEntries(Object.entries(result).map(([k, v]) => [k, String(v)])) });
    }
    catch (error)
    {
        self.postMessage({ id: event.data.id, error: String(error) });
    }
};
