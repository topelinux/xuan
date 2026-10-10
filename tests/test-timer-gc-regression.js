import assert from 'tjs:assert';

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function runFreshTimerCallbacks(iterations, delay) {
    for (let i = 0; i < iterations; i++) {
        await new Promise(resolve => setTimeout(() => resolve(), delay));
    }
}

tjs.engine.gc.run();
await sleep(20);
tjs.engine.gc.run();

const before = tjs.engine.gc.get();

await runFreshTimerCallbacks(200, 1);

await sleep(20);
tjs.engine.gc.run();
await sleep(20);
tjs.engine.gc.run();

const after = tjs.engine.gc.get();
const leakedObjects = after.objCount - before.objCount;

assert.ok(
    leakedObjects < 20,
    `fresh one-shot timers should not leak JS objects: before=${before.objCount}, after=${after.objCount}, delta=${leakedObjects}`
);
