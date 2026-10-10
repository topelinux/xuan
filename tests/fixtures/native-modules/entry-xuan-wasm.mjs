import path from 'tjs:path';
const { default: wasm } = await import('wasm');

const data = await tjs.readFile(path.join(import.meta.dirname, '..', '..', 'wasm', 'i32.wasm'));
const { instance } = await wasm.instantiate(data);

console.log(JSON.stringify({
    add: instance.exports.add(1, 2),
    sub: instance.exports.sub(5, 3),
}));
