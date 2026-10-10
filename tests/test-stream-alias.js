import assert from 'tjs:assert';
import stream from 'stream';
import nodeStream from 'node:stream';

const { Readable, Writable, PassThrough } = stream;

const readable = new Readable();
const passthrough = new PassThrough();

let output = '';
const writable = new Writable({
    write(chunk, _encoding, callback) {
        output += new TextDecoder().decode(chunk);
        callback(null);
    }
});

readable.pipe(passthrough).pipe(writable);
readable.push('hello');
readable.push(' world');
readable.push(null);

await new Promise((resolve, reject) => {
    writable.on('finish', resolve);
    writable.on('error', reject);
});

assert.eq(output, 'hello world');
assert.eq(typeof nodeStream.Readable, 'function');
