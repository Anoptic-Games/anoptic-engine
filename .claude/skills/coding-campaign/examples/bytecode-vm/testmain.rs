// Fitness harness. Calls a::assemble, c::optimize, b::run over the battery.
// Panics per-case are caught (case fails, others continue); infinite loops die to the outer timeout.
fn main() {
    // (assembly source, Some(expected outputs) | None = expect an error somewhere)
    let battery: &[(&str, Option<&[i64]>)] = &[
        ("push 3\npush 4\nadd\nprint\nhalt\n", Some(&[7])),
        ("push 10\npush 3\nsub\nprint\nhalt\n", Some(&[7])),
        ("push 6\npush 7\nmul\nprint\nhalt\n", Some(&[42])),
        ("push 20\npush 6\ndiv\nprint\nhalt\n", Some(&[3])),
        ("push 20\npush 6\nmod\nprint\nhalt\n", Some(&[2])),
        ("push 5\nneg\nprint\nhalt\n", Some(&[-5])),
        ("push 3\ndup\nadd\nprint\nhalt\n", Some(&[6])),
        ("push 1\npush 2\nswap\nsub\nprint\nhalt\n", Some(&[1])),
        ("push 9\npush 8\npop\nprint\nhalt\n", Some(&[9])),
        ("push 1\nprint\npush 2\nprint\npush 3\nprint\nhalt\n", Some(&[1, 2, 3])),
        ("push 2\npush 3\npush 4\nmul\nadd\nprint\nhalt\n", Some(&[14])),
        ("push 3\nloop:\ndup\njz end\ndup\nprint\npush 1\nsub\njmp loop\nend:\nhalt\n", Some(&[3, 2, 1])),
        ("push 100\npush 200\nadd\npush 300\nadd\nprint\nhalt\n", Some(&[600])),
        ("push 2\npush 3\nadd\ndup\njz skip\nprint\njmp done\nskip:\npush 0\nprint\ndone:\nhalt\n", Some(&[5])),
        ("push 1\npush 0\ndiv\nprint\nhalt\n", None),
        ("push 1\nadd\nhalt\n", None),
    ];

    std::panic::set_hook(Box::new(|_| {}));

    let mut pass = 0usize;
    let total = battery.len();
    let mut in_ops = 0usize;
    let mut out_ops = 0usize;

    for (src, exp) in battery {
        let src = *src;
        let exp = *exp;
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            match a::assemble(src) {
                Err(_) => (exp.is_none(), 0usize, 0usize),
                Ok(prog) => {
                    let opt = c::optimize(&prog);
                    let io = prog.len();
                    let oo = opt.len();
                    match b::run(&opt) {
                        Err(_) => (exp.is_none(), io, oo),
                        Ok(got) => match exp {
                            Some(e) => (got.as_slice() == e, io, oo),
                            None => (false, io, oo),
                        },
                    }
                }
            }
        }));
        if let Ok((p, io, oo)) = r {
            if p {
                pass += 1;
            }
            in_ops += io;
            out_ops += oo;
        }
    }

    println!(
        "FITNESS pass={}/{} in_ops={} out_ops={} reduction={}",
        pass,
        total,
        in_ops,
        out_ops,
        in_ops.saturating_sub(out_ops)
    );
}
