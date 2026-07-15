// Golden VM. Stack machine, returns printed outputs. pub fn run(&[Op]) -> Result<Vec<i64>, String>.
pub fn run(program: &[Op]) -> Result<Vec<i64>, String> {
    let mut st: Vec<i64> = Vec::new();
    let mut out: Vec<i64> = Vec::new();
    let mut pc: usize = 0;
    let mut steps = 0usize;
    loop {
        if pc >= program.len() {
            return Err("pc out of bounds".to_string());
        }
        steps += 1;
        if steps > 10_000_000 {
            return Err("step limit".to_string());
        }
        match program[pc] {
            Op::Push(n) => {
                st.push(n);
                pc += 1;
            }
            Op::Pop => {
                g_pop(&mut st)?;
                pc += 1;
            }
            Op::Add => {
                let b = g_pop(&mut st)?;
                let a = g_pop(&mut st)?;
                st.push(a.wrapping_add(b));
                pc += 1;
            }
            Op::Sub => {
                let b = g_pop(&mut st)?;
                let a = g_pop(&mut st)?;
                st.push(a.wrapping_sub(b));
                pc += 1;
            }
            Op::Mul => {
                let b = g_pop(&mut st)?;
                let a = g_pop(&mut st)?;
                st.push(a.wrapping_mul(b));
                pc += 1;
            }
            Op::Div => {
                let b = g_pop(&mut st)?;
                let a = g_pop(&mut st)?;
                if b == 0 {
                    return Err("div by zero".to_string());
                }
                st.push(a.wrapping_div(b));
                pc += 1;
            }
            Op::Mod => {
                let b = g_pop(&mut st)?;
                let a = g_pop(&mut st)?;
                if b == 0 {
                    return Err("mod by zero".to_string());
                }
                st.push(a.wrapping_rem(b));
                pc += 1;
            }
            Op::Neg => {
                let a = g_pop(&mut st)?;
                st.push(a.wrapping_neg());
                pc += 1;
            }
            Op::Dup => {
                let a = *st.last().ok_or_else(|| "underflow".to_string())?;
                st.push(a);
                pc += 1;
            }
            Op::Swap => {
                let n = st.len();
                if n < 2 {
                    return Err("underflow".to_string());
                }
                st.swap(n - 1, n - 2);
                pc += 1;
            }
            Op::Jmp(t) => {
                pc = t;
            }
            Op::Jz(t) => {
                let a = g_pop(&mut st)?;
                if a == 0 {
                    pc = t;
                } else {
                    pc += 1;
                }
            }
            Op::Print => {
                let a = g_pop(&mut st)?;
                out.push(a);
                pc += 1;
            }
            Op::Halt => {
                return Ok(out);
            }
        }
    }
}

fn g_pop(st: &mut Vec<i64>) -> Result<i64, String> {
    st.pop().ok_or_else(|| "stack underflow".to_string())
}
