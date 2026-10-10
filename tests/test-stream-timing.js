import assert from 'tjs:assert';
const { Readable, Writable } = require('node:stream');

// Test stream emitClose and event timing compatibility
// These fixes were needed for multer/busboy async file upload patterns

// === Readable push(null) should not emit 'end' when not flowing ===

{
    const r = new Readable();
    r.push('data');
    r.push(null);  // End the stream

    // Should NOT have emitted 'end' yet since not in flowing mode
    let endCalled = false;
    r.on('end', () => { endCalled = true; });

    // Start flowing - NOW 'end' should fire
    r.resume();

    await new Promise(resolve => setTimeout(resolve, 50));
    assert.ok(endCalled, 'end emitted after resume on pre-ended stream');
}

// === Writable end should emit finish then close ===

{
    const w = new Writable();
    w._write = (chunk, enc, cb) => cb(null);

    const events = [];
    w.on('finish', () => events.push('finish'));
    w.on('close', () => events.push('close'));

    w.write('x');
    w.end();

    await new Promise(resolve => w.on('close', resolve));
    assert.ok(events.includes('finish'), 'writable emits finish');
    assert.ok(events.includes('close'), 'writable emits close');
    assert.ok(events.indexOf('finish') < events.indexOf('close'), 'finish before close');
}

// === Pipe handles already-ended stream with buffered data ===

{
    const src = new Readable();
    src.push('already buffered');
    src.push(null);  // End immediately

    // Now pipe - the buffered data should still be written
    const destChunks = [];
    const dest = new Writable();
    dest._write = (chunk, enc, cb) => {
        // chunk is Uint8Array, convert to string
        destChunks.push(new TextDecoder().decode(chunk));
        cb(null);
    };

    src.pipe(dest);

    await new Promise(resolve => dest.on('finish', resolve));
    assert.equal(destChunks.join(''), 'already buffered', 'pipe handles pre-ended buffered data');
}

// === emitClose: false option ===

{
    const w = new Writable({ emitClose: false });
    w._write = (chunk, enc, cb) => cb(null);

    let closeEmitted = false;
    w.on('close', () => { closeEmitted = true; });

    w.write('x');
    w.end();

    await new Promise(resolve => w.on('finish', resolve));
    assert.ok(!closeEmitted, 'emitClose: false suppresses close event');
}

// === emitClose: false on Readable ===

{
    const r = new Readable({ emitClose: false });
    r._read = () => {};
    r.push('data');
    r.push(null);

    let closeEmitted = false;
    r.on('close', () => { closeEmitted = true; });

    r.resume();
    await new Promise(resolve => r.on('end', resolve));
    assert.ok(!closeEmitted, 'Readable emitClose: false suppresses close');
}

// === Async pipe setup pattern (like multer with crypto.randomBytes) ===

{
    const crypto = require('node:crypto');

    // Simulate busboy file stream that ends before async callback fires
    const fileStream = new Readable();
    fileStream._read = () => {};
    fileStream.push('file content here');
    fileStream.push(null);  // File stream ended

    let writeStreamClosed = false;
    let writtenBytes = 0;

    // Simulate multer's async crypto.randomBytes callback pattern
    await new Promise((resolve) => {
        crypto.randomBytes(8, (err, raw) => {
            // At this point, fileStream has already ended
            // But we now set up the WriteStream and pipe
            const { Writable: W } = require('node:stream');

            const writeStream = new W();
            let buf = new Uint8Array(0);
            writeStream._write = (chunk, enc, cb) => {
                const newBuf = new Uint8Array(buf.length + chunk.length);
                newBuf.set(buf);
                newBuf.set(chunk, buf.length);
                buf = newBuf;
                writtenBytes += chunk.length;
                cb(null);
            };
            writeStream.on('close', () => {
                writeStreamClosed = true;
                resolve();
            });

            // This is the critical pattern: pipe an already-ended stream
            fileStream.pipe(writeStream);
        });
    });

    assert.ok(writeStreamClosed, 'writeStream closed after async pipe setup');
    assert.equal(writtenBytes, 'file content here'.length, 'async pipe written bytes count');
}
