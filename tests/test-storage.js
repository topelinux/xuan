import assert from 'tjs:assert';
import path from 'tjs:path';

function getLocalStorageOrNull() {
    try {
        return globalThis.localStorage;
    } catch (_) {
        return null;
    }
}

const localStorage = getLocalStorageOrNull();
if (!localStorage) {
    tjs.exit(0);
}

if (tjs.env.TJS_HOME) {
    // This is the second test.
    assert.eq(localStorage.getItem('foo'), '123');

    tjs.exit(0);
}

localStorage.clear();

assert.eq(localStorage.getItem('foo'), null);

localStorage.setItem('foo', 123);

assert.eq(localStorage.getItem('foo'), '123');

const args = [
    tjs.exePath,
    'run',
    import.meta.path
];
const proc = tjs.spawn(args, { env: { TJS_HOME: path.join(import.meta.dirname, 'fixtures') } });
const status = await proc.wait();

assert.ok(status.exit_status === 0 && status.term_signal === null, 'succeeded')
