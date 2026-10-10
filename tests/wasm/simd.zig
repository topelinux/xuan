const std = @import("std");
var scratch: [96]f32 = [_]f32{0} ** 96;

export fn add_simd(a: [*]const f32, b: [*]const f32, out: [*]f32, len: usize) void {
    var i: usize = 0;

    // 每次处理 4 个 f32（128-bit）
    while (i + 4 <= len) : (i += 4) {
        const va = @as(@Vector(4, f32), a[i..][0..4].*);
        const vb = @as(@Vector(4, f32), b[i..][0..4].*);

        const vc = va + vb;

        out[i..][0..4].* = vc;
    }

    // tail 处理
    while (i < len) : (i += 1) {
        out[i] = a[i] + b[i];
    }
}

// Small scalar API that still forces wasm SIMD opcodes in codegen.
export fn sum8_simd(a: i32, b: i32) i32 {
    const aa: i16 = @intCast(a);
    const bb: i16 = @intCast(b);
    const va: @Vector(8, i16) = @splat(aa);
    const vb: @Vector(8, i16) = @splat(bb);
    const vc = va + vb;
    return @as(i32, vc[0]);
}

export fn add_i16x8_once(a: [*]const i16, b: [*]const i16, out: [*]i16) void {
    const va = @as(@Vector(8, i16), a[0..8].*);
    const vb = @as(@Vector(8, i16), b[0..8].*);
    out[0..8].* = va + vb;
}

export fn fill_i16x8_splat(out: [*]i16, val: i32) void {
    const vv: @Vector(8, i16) = @splat(@as(i16, @intCast(val)));
    out[0..8].* = vv;
}

export fn scratch_ptr() i32 {
    return @as(i32, @intCast(@intFromPtr(&scratch[0])));
}

export fn set_f32_at(byte_ptr: i32, value: f32) void {
    const p: [*]f32 = @ptrFromInt(@as(usize, @intCast(byte_ptr)));
    p[0] = value;
}

export fn get_f32_at(byte_ptr: i32) f32 {
    const p: [*]const f32 = @ptrFromInt(@as(usize, @intCast(byte_ptr)));
    return p[0];
}
