import assert from 'tjs:assert';
import path from 'tjs:path';

const wasm = await import('wasm');

const expectedRaw = tjs.env.TJS_EXPECT_WASM_SIMD;
const expected = expectedRaw === undefined ? null : (expectedRaw === '1' || expectedRaw === 'true' || expectedRaw === 'ON');

const bytes = await tjs.readFile(path.join(import.meta.dirname, 'wasm', 'simd_i16_add.wasm'));

const buildFlag = wasm?.native?.isSimdEnabled?.() ?? null;
let supported = false;
let errorMessage = '';

if (buildFlag === false) {
    errorMessage = 'SIMD disabled in build';
} else {
    try {
        const { instance } = await WebAssembly.instantiate(bytes);
        assert.eq(instance.exports.sum8(19, 23), 42);
        supported = true;
    } catch (e) {
        errorMessage = String(e?.message ?? e);
    }
}

if (expected !== null) {
    assert.eq(supported, expected, `SIMD support mismatch (expected=${expected}, got=${supported}, err='${errorMessage}')`);
}

console.log(JSON.stringify({
    expected,
    supported,
    buildFlag: buildFlag ?? null,
    error: errorMessage || null
}));
