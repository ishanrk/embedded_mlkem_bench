use pqc_poly_ring::{mul, PolyArray, SignedPolyArray, N, Q};
use std::time::Instant;

#[derive(Debug, serde::Serialize)]
pub struct DesignResult
{
    pub id: String,
    pub algorithm: String,
    pub schedule: String,
    pub temporary_bytes: usize,
    pub additions: u64,
    pub multiplications: u64,
    pub loads: u64,
    pub stores: u64,
    pub host_nanoseconds: Option<u64>,
    pub riscv_cycles: Option<u64>,
    pub custom_instructions: Vec<String>,
    pub extension_area: Option<u64>,
    pub verified_against_reference: bool,
}

#[derive(Default)]
struct Cnt
{
    a: u64,
    m: u64,
    l: u64,
    s: u64,
    n: usize,
    p: usize,
}

impl Cnt
{
    fn take(&mut self, n: usize)
    {
        self.n += n * 2;
        self.p = self.p.max(self.n);
        self.s += n as u64;
    }

    fn give(&mut self, n: usize)
    {
        self.n -= n * 2;
    }
}

fn red(x: i32) -> u16
{
    (x as u16) & (Q - 1)
}

fn add(x: &[u16], y: &[u16], c: &mut Cnt) -> Vec<u16>
{
    let mut z = Vec::with_capacity(x.len());

    c.take(x.len());

    for (i, v) in x.iter().enumerate()
    {
        let w = y.get(i).copied().unwrap_or(0);

        z.push(red(i32::from(*v) + i32::from(w)));
        c.a += 1;
        c.l += 1 + u64::from(i < y.len());
    }

    z
}

fn sub(x: &mut [u16], y: &[u16], c: &mut Cnt)
{
    for (v, w) in x.iter_mut().zip(y)
    {
        *v = red(i32::from(*v) - i32::from(*w));
        c.a += 1;
        c.l += 2;
        c.s += 1;
    }
}

fn sb(x: &[u16], y: &[u16], c: &mut Cnt) -> Vec<u16>
{
    let n = x.len();
    let mut z = vec![0u16; n * 2 - 1];

    c.take(z.len());

    for (i, v) in x.iter().enumerate()
    {
        c.l += 1;

        for (j, w) in y.iter().enumerate()
        {
            let k = i + j;
            let p = u32::from(z[k]) + u32::from(*v) * u32::from(*w);

            z[k] = (p & u32::from(Q - 1)) as u16;
            c.a += 1;
            c.m += 1;
            c.l += 2;
            c.s += 1;
        }
    }

    z
}

fn kar(x: &[u16], y: &[u16], n: usize, c: &mut Cnt) -> Vec<u16>
{
    if x.len() <= n
    {
        return sb(x, y, c);
    }

    let m = x.len() / 2;
    let z0 = kar(&x[..m], &y[..m], n, c);
    let z2 = kar(&x[m..], &y[m..], n, c);
    let sx = add(&x[..m], &x[m..], c);
    let sy = add(&y[..m], &y[m..], c);
    let mut z1 = kar(&sx, &sy, n, c);

    c.give(sx.len());
    c.give(sy.len());
    drop(sx);
    drop(sy);
    sub(&mut z1, &z0, c);
    sub(&mut z1, &z2, c);

    let mut z = vec![0u16; x.len() * 2 - 1];

    c.take(z.len());
    put(&mut z, &z0, 0, c);
    put(&mut z, &z1, m, c);
    put(&mut z, &z2, m * 2, c);
    c.give(z0.len());
    c.give(z1.len());
    c.give(z2.len());
    drop(z0);
    drop(z1);
    drop(z2);
    z
}

fn put(z: &mut [u16], x: &[u16], n: usize, c: &mut Cnt)
{
    for (v, w) in z[n..].iter_mut().zip(x)
    {
        *v = red(i32::from(*v) + i32::from(*w));
        c.a += 1;
        c.l += 2;
        c.s += 1;
    }
}

fn norm(a: &SignedPolyArray, c: &mut Cnt) -> Vec<u16>
{
    let mut x = Vec::with_capacity(N);

    c.take(N);

    for v in a
    {
        x.push(red(i32::from(*v)));
        c.l += 1;
    }

    x
}

fn pad(a: &SignedPolyArray, c: &mut Cnt) -> Vec<u16>
{
    let mut x = vec![0u16; 512];

    c.take(x.len());

    for (i, v) in a.iter().enumerate()
    {
        x[i] = red(i32::from(*v));
        c.l += 1;
        c.s += 1;
    }

    x
}

fn fold(x: &[u16], c: &mut Cnt) -> PolyArray
{
    let mut y = [0u16; N];

    c.s += N as u64;

    for (i, v) in x.iter().enumerate()
    {
        let k = i % N;

        y[k] = red(i32::from(y[k]) + i32::from(*v));
        c.a += 1;
        c.l += 2;
        c.s += 1;
    }

    y
}

fn result(id: &str, alg: &str, sch: &str, c: Cnt, ns: u64, ok: bool) -> DesignResult
{
    DesignResult
    {
        id: id.into(),
        algorithm: alg.into(),
        schedule: sch.into(),
        temporary_bytes: c.p,
        additions: c.a,
        multiplications: c.m,
        loads: c.l,
        stores: c.s,
        host_nanoseconds: Some(ns),
        riscv_cycles: None,
        custom_instructions: Vec::new(),
        extension_area: None,
        verified_against_reference: ok,
    }
}

fn school(a: &SignedPolyArray, b: &SignedPolyArray, c: &mut Cnt) -> PolyArray
{
    let mut z = [0u16; N];

    c.s += N as u64;

    for (i, v) in a.iter().enumerate()
    {
        let x = red(i32::from(*v));

        c.l += 1;

        for (j, w) in b.iter().enumerate()
        {
            let k = if i + j >= N
            {
                i + j - N
            }
            else
            {
                i + j
            };
            let y = red(i32::from(*w));
            let p = u32::from(z[k]) + u32::from(x) * u32::from(y);

            z[k] = (p & u32::from(Q - 1)) as u16;
            c.a += 1;
            c.m += 1;
            c.l += 2;
            c.s += 1;
        }
    }

    z
}

fn run_k(a: &SignedPolyArray, b: &SignedPolyArray, n: usize, id: &str, sch: &str) -> (PolyArray, DesignResult)
{
    let mut c = Cnt::default();
    let t = Instant::now();
    let x = pad(a, &mut c);
    let y = pad(b, &mut c);
    let z = kar(&x, &y, n, &mut c);
    let d = fold(&z[..N * 2 - 1], &mut c);
    let ns = t.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    c.give(x.len());
    c.give(y.len());
    c.give(z.len());
    debug_assert_eq!(c.n, 0);

    let ok = d == mul(a, b);
    let r = result(id, "karatsuba", sch, c, ns, ok);

    (d, r)
}

fn run_top(a: &SignedPolyArray, b: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    let mut c = Cnt::default();
    let t = Instant::now();
    let x = norm(a, &mut c);
    let y = norm(b, &mut c);
    let z0 = sb(&x[..255], &y[..255], &mut c);
    let z2 = sb(&x[255..], &y[255..], &mut c);
    let sx = add(&x[..255], &x[255..], &mut c);
    let sy = add(&y[..255], &y[255..], &mut c);
    let mut z1 = sb(&sx, &sy, &mut c);

    c.give(sx.len());
    c.give(sy.len());
    drop(sx);
    drop(sy);
    sub(&mut z1, &z0, &mut c);
    sub(&mut z1, &z2, &mut c);

    let mut z = vec![0u16; N * 2 - 1];

    c.take(z.len());
    put(&mut z, &z0, 0, &mut c);
    put(&mut z, &z1, 255, &mut c);
    put(&mut z, &z2, 510, &mut c);
    c.give(z0.len());
    c.give(z1.len());
    c.give(z2.len());
    drop(z0);
    drop(z1);
    drop(z2);

    let d = fold(&z, &mut c);
    let ns = t.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    c.give(x.len());
    c.give(y.len());
    c.give(z.len());
    debug_assert_eq!(c.n, 0);

    let ok = d == mul(a, b);
    let r = result("karatsuba-one-split", "karatsuba", "split-255-schoolbook", c, ns, ok);

    (d, r)
}

pub fn schoolbook_509(a: &SignedPolyArray, b: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    let mut c = Cnt::default();
    let t = Instant::now();
    let d = school(a, b, &mut c);
    let ns = t.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;
    let ok = d == mul(a, b);
    let r = result("schoolbook-509", "schoolbook", "direct-cyclic", c, ns, ok);

    (d, r)
}

pub fn karatsuba_top(a: &SignedPolyArray, b: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_top(a, b)
}

pub fn karatsuba_32(a: &SignedPolyArray, b: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_k(a, b, 32, "karatsuba-32", "recursive-32")
}

pub fn karatsuba_16(a: &SignedPolyArray, b: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_k(a, b, 16, "karatsuba-16", "recursive-16")
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn designs()
    {
        let mut a = [0i16; N];
        let mut b = [0i16; N];

        for i in 0..N
        {
            a[i] = ((i * 37 + 11) % 4096) as i16 - 2048;
            b[i] = ((i * 73 + 19) % 4096) as i16 - 2048;
        }

        let x = [
            schoolbook_509(&a, &b),
            karatsuba_top(&a, &b),
            karatsuba_32(&a, &b),
            karatsuba_16(&a, &b),
        ];
        let m = [259081, 194566, 82944, 62208];

        for ((_, r), n) in x.into_iter().zip(m)
        {
            assert!(r.verified_against_reference);
            assert_eq!(r.multiplications, n);
            assert!(r.loads > 0);
            assert!(r.stores > 0);
            assert_eq!(r.riscv_cycles, None);
            assert_eq!(r.extension_area, None);
        }
    }
}
