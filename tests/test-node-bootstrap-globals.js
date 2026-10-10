import assert from 'tjs:assert';

function pick(name) {
    const desc = Object.getOwnPropertyDescriptor(globalThis, name);
    return {
        enumerable: desc?.enumerable,
        writable: desc?.writable,
        configurable: desc?.configurable,
    };
}

assert.eq(global, globalThis, 'global aliases globalThis');

const globalDesc = pick('global');
assert.eq(globalDesc.enumerable, true, 'global is enumerable');
assert.eq(globalDesc.writable, true, 'global is writable');
assert.eq(globalDesc.configurable, true, 'global is configurable');

const processDesc = pick('process');
assert.eq(processDesc.enumerable, false, 'process is non-enumerable');
assert.eq(processDesc.writable, true, 'process is writable');
assert.eq(processDesc.configurable, true, 'process is configurable');

const bufferDesc = pick('Buffer');
assert.eq(bufferDesc.enumerable, false, 'Buffer is non-enumerable');
assert.eq(bufferDesc.writable, true, 'Buffer is writable');
assert.eq(bufferDesc.configurable, true, 'Buffer is configurable');

const immediateDesc = pick('setImmediate');
assert.eq(immediateDesc.enumerable, true, 'setImmediate is enumerable');
assert.eq(immediateDesc.writable, true, 'setImmediate is writable');
assert.eq(immediateDesc.configurable, true, 'setImmediate is configurable');
