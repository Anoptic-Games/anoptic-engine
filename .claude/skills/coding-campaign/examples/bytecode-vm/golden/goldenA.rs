// Golden assembler. Two-pass: resolve labels, then emit. pub fn assemble(&str) -> Result<Vec<Op>, String>.
pub fn assemble(src: &str) -> Result<Vec<Op>, String> {
    let mut labels: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut idx = 0usize;
    for raw in src.lines() {
        let line = g_strip(raw);
        if line.is_empty() {
            continue;
        }
        if line.ends_with(':') {
            let name = line[..line.len() - 1].trim();
            if !g_ident(name) {
                return Err(format!("bad label: {}", line));
            }
            labels.insert(name.to_string(), idx);
        } else {
            idx += 1;
        }
    }
    let mut out: Vec<Op> = Vec::new();
    for raw in src.lines() {
        let line = g_strip(raw);
        if line.is_empty() || line.ends_with(':') {
            continue;
        }
        let mut it = line.split_whitespace();
        let mn = it.next().unwrap();
        let op = match mn {
            "push" => {
                let a = it.next().ok_or_else(|| "push needs operand".to_string())?;
                let n: i64 = a.parse().map_err(|_| format!("bad int: {}", a))?;
                Op::Push(n)
            }
            "pop" => Op::Pop,
            "add" => Op::Add,
            "sub" => Op::Sub,
            "mul" => Op::Mul,
            "div" => Op::Div,
            "mod" => Op::Mod,
            "neg" => Op::Neg,
            "dup" => Op::Dup,
            "swap" => Op::Swap,
            "print" => Op::Print,
            "halt" => Op::Halt,
            "jmp" | "jz" => {
                let l = it.next().ok_or_else(|| format!("{} needs label", mn))?;
                let t = *labels.get(l).ok_or_else(|| format!("undefined label: {}", l))?;
                if mn == "jmp" { Op::Jmp(t) } else { Op::Jz(t) }
            }
            other => return Err(format!("unknown mnemonic: {}", other)),
        };
        if let Some(extra) = it.next() {
            return Err(format!("trailing token: {}", extra));
        }
        out.push(op);
    }
    Ok(out)
}

fn g_strip(s: &str) -> &str {
    let s = match s.find(';') {
        Some(i) => &s[..i],
        None => s,
    };
    s.trim()
}

fn g_ident(s: &str) -> bool {
    let mut c = s.chars();
    match c.next() {
        Some(ch) if ch == '_' || ch.is_ascii_alphabetic() => {}
        _ => return false,
    }
    c.all(|ch| ch == '_' || ch.is_ascii_alphanumeric())
}
