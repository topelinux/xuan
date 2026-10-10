import assert from 'tjs:assert';

const seen = [];
function onUncaught(err, origin) {
    seen.push({ message: err.message, code: err.code, origin });
}

process.on('uncaughtException', onUncaught);

queueMicrotask(() => {
    const err = new Error('micro boom');
    err.code = 'MICRO';
    throw err;
});

setTimeout(() => {
    const err = new Error('timer boom');
    err.code = 'TIMER';
    throw err;
}, 0);

await new Promise((resolve) => setTimeout(resolve, 40));
process.off('uncaughtException', onUncaught);

assert.eq(seen.length, 2, 'uncaughtException handles microtask and timer throws');
assert.eq(seen[0], { message: 'micro boom', code: 'MICRO', origin: 'uncaughtException' });
assert.eq(seen[1], { message: 'timer boom', code: 'TIMER', origin: 'uncaughtException' });
