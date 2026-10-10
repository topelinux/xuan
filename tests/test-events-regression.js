import assert from 'tjs:assert';
import EventEmitter from 'tjs:events';

async function runOnceRegression(iterations = 20) {
    tjs.engine.gc.run();
    const before = tjs.engine.gc.get();

    for (let i = 0; i < iterations; i++) {
        const emitter = new EventEmitter();
        await new Promise(resolve => {
            emitter.once('data', () => resolve(undefined));
            emitter.emit('data', i);
        });
    }

    tjs.engine.gc.run();
    const after = tjs.engine.gc.get();
    const delta = after.objCount - before.objCount;

    // Small GC noise is acceptable, but once() must not retain a per-iteration object chain.
    assert.ok(delta <= 20, `EventEmitter.once() leaked too many objects: delta=${delta}`);
}

await runOnceRegression();
