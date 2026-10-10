import assert from 'tjs:assert';


assert.ok(tjs.version, 'tjs.version is defined');
assert.ok(tjs.engine.versions, 'tjs.versions is defined');
assert.ok(tjs.engine.versions.tjs, 'tjs is defined');
assert.ok(tjs.engine.versions.lws, 'lws is defined');
assert.ok(tjs.engine.versions.quickjs, 'quickjs is defined');
assert.ok(tjs.engine.versions.uv, 'uv is defined');

if (tjs.engine.versions.sqlite3) {
    assert.ok(tjs.engine.versions.sqlite3, 'sqlite3 is defined');
} else {
    const sqlite = await import('sqlite');
    assert.ok(sqlite?.Database, 'sqlite native module is importable');
}

if (tjs.engine.versions.wamr) {
    assert.ok(tjs.engine.versions.wamr, 'wamr is defined');
} else {
    const wasm = await import('wasm');
    assert.ok(wasm?.WebAssembly || wasm?.default, 'wasm native module is importable');
}
