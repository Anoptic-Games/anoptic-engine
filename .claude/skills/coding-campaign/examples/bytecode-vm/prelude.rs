// Fixed ISA shared by all candidates. Concatenated at crate root; never redefined by candidates.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Op {
    Push(i64),
    Pop,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Dup,
    Swap,
    Jmp(usize),
    Jz(usize),
    Print,
    Halt,
}
