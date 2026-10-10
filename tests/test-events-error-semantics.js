import assert from 'tjs:assert';
import { EventEmitter } from 'node:events';

const emitter = new EventEmitter();
const err = Object.assign(new Error('boom'), { code: 'TEST_ERROR' });

let caught;
try {
    emitter.emit('error', err);
} catch (e) {
    caught = e;
}

assert.eq(caught, err, 'unhandled error events throw the emitted Error');

const handled = new EventEmitter();
let handledError;
handled.on('error', (e) => {
    handledError = e;
});

assert.eq(handled.emit('error', err), true, 'handled error events return true');
assert.eq(handledError, err, 'handled error listener receives the Error');
