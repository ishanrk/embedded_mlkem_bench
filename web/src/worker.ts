import { arithmetic, operand, type Instruction } from './model';
// keep arbitrary-precision arithmetic off the UI event loop and tag replies to drop stale work
self.onmessage = (event: MessageEvent<{ id: number; kind: Instruction; a: string; b: string; shift: number }>) =>
{
    try
    {
        const { id, kind, a, b, shift } = event.data;
        const result = arithmetic(kind, operand(a), operand(b), shift);
        // structured cloning of BigInt is awkward across older browsers, so send decimal strings
        self.postMessage({ id, result: Object.fromEntries(Object.entries(result).map(([k, v]) => [k, String(v)])) });
    }
    catch (error)
    {
        self.postMessage({ id: event.data.id, error: String(error) });
    }
};
