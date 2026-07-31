#![no_std]

pub const N: usize = 509;
pub const Q: u16 = 2048;

pub type Poly = [i16; N];
pub type Out = [u16; N];

pub fn mul(a: &Poly, b: &Poly) -> Out
{
    let mut c = [0i64; N];

    for (i, x) in a.iter().enumerate()
    {
        for (j, y) in b.iter().enumerate()
        {
            let s = i + j;
            let k = if s >= N
            {
                s - N
            }
            else
            {
                s
            };

            c[k] += i64::from(*x) * i64::from(*y);
        }
    }

    let mut d = [0u16; N];

    for (i, x) in c.iter().enumerate()
    {
        d[i] = x.rem_euclid(i64::from(Q)) as u16;
    }

    d
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn mul_one()
    {
        let mut a = [0i16; N];
        let mut b = [0i16; N];
        let mut c = [0u16; N];

        a[0] = 3;
        a[508] = -2;
        b[0] = 5;
        b[1] = 7;
        c[0] = 1;
        c[1] = 21;
        c[508] = 2038;

        assert_eq!(mul(&a, &b), c);
    }
}
