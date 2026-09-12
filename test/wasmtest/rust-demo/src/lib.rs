mod bindings;

use bindings::Guest;
use bindings::{Color, Maybe, Pair, Perms};

struct Component;

impl Guest for Component {
    fn add(a: u32, b: u32) -> u32 {
        a.wrapping_add(b)
    }
    fn sub(a: i32, b: i32) -> i32 {
        a.wrapping_sub(b)
    }
    fn mul_f(a: f32, b: f32) -> f32 {
        a * b
    }
    fn div_d(a: f64, b: f64) -> f64 {
        a / b
    }
    fn greet(name: String) -> String {
        format!("Hello, {}!", name)
    }
    fn sum_list(xs: Vec<u32>) -> u32 {
        xs.iter().fold(0u32, |acc, x| acc.wrapping_add(*x))
    }
    fn double_list(xs: Vec<u32>) -> Vec<u32> {
        xs.iter().map(|x| x.wrapping_mul(2)).collect()
    }
    fn make_pair(a: u32, b: u32) -> Pair {
        Pair { x: a, y: b }
    }
    fn swap_pair(p: Pair) -> Pair {
        Pair { x: p.y, y: p.x }
    }
    fn try_get(flag: bool) -> Result<u32, String> {
        if flag {
            Ok(42)
        } else {
            Err("boom".to_string())
        }
    }
    fn pick(choice: Color, val: u32) -> Option<u32> {
        match choice {
            Color::Red => Some(val),
            Color::Green => None,
            Color::Blue => Some(val.wrapping_add(1)),
        }
    }
    fn echo_maybe(v: Maybe) -> Maybe {
        v
    }
    fn count_flags(f: Perms) -> u32 {
        let mut n = 0u32;
        if f.contains(bindings::Perms::READ) {
            n += 1;
        }
        if f.contains(bindings::Perms::WRITE) {
            n += 1;
        }
        if f.contains(bindings::Perms::EXEC) {
            n += 1;
        }
        n
    }
    fn not_bool(b: bool) -> bool {
        !b
    }
}

bindings::export!(Component with_types_in bindings);
