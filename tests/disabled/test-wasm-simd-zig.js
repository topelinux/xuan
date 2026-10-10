import assert from 'tjs:assert';
import path from 'tjs:path';

await import('wasm');

const wasmPath = path.join(import.meta.dirname, 'wasm', 'simd_zig.wasm');
const bytes = new Uint8Array(await tjs.readFile(wasmPath));

let hasSimdOpcode = false;
for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === 0xfd) {
        hasSimdOpcode = true;
        break;
    }
}
assert.ok(hasSimdOpcode, 'zig wasm should contain SIMD opcodes (0xfd prefix)');

const module = await WebAssembly.compile(bytes);
const exports = WebAssembly.Module.exports(module);
const { instance } = await WebAssembly.instantiate(bytes);
assert.eq(instance.exports.sum8_simd(19, 23), 42);

const len = 8;
const base = instance.exports.scratch_ptr();
const a = base;
const b = base + len * 4;
const out = base + len * 2 * 4;

for (let i = 0; i < len; i++) {
    instance.exports.set_f32_at(a + i * 4, i);
    instance.exports.set_f32_at(b + i * 4, i * 2);
}

instance.exports.add_simd(a, b, out, len);

const vecOut = [];
for (let i = 0; i < len; i++) {
    const got = instance.exports.get_f32_at(out + i * 4);
    const expected = i * 3;
    assert.ok(Math.abs(got - expected) < 1e-6, `vec add mismatch at i=${i}: got=${got}, expected=${expected}`);
    vecOut.push(got);
}

console.log(JSON.stringify({
    wasmPath,
    hasSimdOpcode,
    exports,
    vecOut
}));
