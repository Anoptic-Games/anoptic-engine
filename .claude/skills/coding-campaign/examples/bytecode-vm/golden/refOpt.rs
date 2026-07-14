// Reference optimizer: fold Push,Push,<binop> and Push,Neg to a constant, fixing jump targets. Validates C-stage scoring.
pub fn optimize(program: &[Op]) -> Vec<Op> {
    let mut prog = program.to_vec();
    loop {
        let mut hit: Option<(usize, Op, usize)> = None; // (start, replacement, removed)
        for i in 0..prog.len() {
            if i + 1 < prog.len() {
                if let (Op::Push(a), Op::Neg) = (prog[i], prog[i + 1]) {
                    hit = Some((i, Op::Push(a.wrapping_neg()), 2));
                    break;
                }
            }
            if i + 2 < prog.len() {
                if let (Op::Push(a), Op::Push(b)) = (prog[i], prog[i + 1]) {
                    let v = match prog[i + 2] {
                        Op::Add => Some(a.wrapping_add(b)),
                        Op::Sub => Some(a.wrapping_sub(b)),
                        Op::Mul => Some(a.wrapping_mul(b)),
                        Op::Div => if b != 0 { Some(a.wrapping_div(b)) } else { None },
                        Op::Mod => if b != 0 { Some(a.wrapping_rem(b)) } else { None },
                        _ => None,
                    };
                    if let Some(v) = v {
                        hit = Some((i, Op::Push(v), 3));
                        break;
                    }
                }
            }
        }
        let (i, replacement, removed) = match hit {
            Some(x) => x,
            None => break,
        };
        let delta = removed - 1;
        let mut next: Vec<Op> = Vec::with_capacity(prog.len() - delta);
        next.extend_from_slice(&prog[..i]);
        next.push(replacement);
        next.extend_from_slice(&prog[i + removed..]);
        for op in next.iter_mut() {
            if let Op::Jmp(t) | Op::Jz(t) = op {
                let old = *t;
                if old >= i + removed {
                    *t = old - delta;
                } else if old > i {
                    *t = i;
                }
            }
        }
        prog = next;
    }
    prog
}
