import assert from 'tjs:assert';
const fs = require('node:fs');
const path = require('node:path');
const { Readable } = require('node:stream');

// Test fs.createWriteStream Node.js compatibility

// Create a temp directory
const tmpDir = await tjs.makeTempDir('test_fs_ws_XXXXXX');

// Clean up function
async function cleanup() {
    try {
        await tjs.remove(tmpDir, { recursive: true });
    } catch (e) {}
}

// Helper to create temp file path
function tmpPath(name) {
    return path.join(tmpDir, name);
}

// === Basic write stream ===

const testPath1 = tmpPath('wstream-basic.txt');
const ws1 = fs.createWriteStream(testPath1);
ws1.write('Hello');
ws1.write(' ');
ws1.write('World');
ws1.end();

const result1 = await new Promise((resolve) => {
    ws1.on('finish', () => {
        resolve(fs.readFileSync(testPath1, 'utf8'));
    });
});
assert.equal(result1, 'Hello World', 'basic write stream');
assert.equal(ws1.bytesWritten, 11, 'bytesWritten tracked');
fs.unlinkSync(testPath1);

// === Write stream events ===

const testPath2 = tmpPath('wstream-events.txt');
const ws2 = fs.createWriteStream(testPath2);
const events = [];
ws2.on('open', () => events.push('open'));
ws2.on('finish', () => events.push('finish'));
ws2.on('close', () => events.push('close'));

ws2.write('test');
ws2.end();

await new Promise(resolve => ws2.on('close', resolve));
assert.ok(events.includes('open'), 'open event emitted');
assert.ok(events.includes('finish'), 'finish event emitted');
assert.ok(events.includes('close'), 'close event emitted');
assert.ok(events.indexOf('finish') < events.indexOf('close'), 'finish before close');
assert.ok(ws2.bytesWritten === 4, 'bytesWritten is 4');
fs.unlinkSync(testPath2);

// === Pipe from Readable to Writable ===

const testPath3 = tmpPath('wstream-pipe.txt');
const srcContent = 'pipe test content\nline2\n';

const readable = new Readable();
readable._read = () => {};
readable.push(srcContent);
readable.push(null);

const ws3 = fs.createWriteStream(testPath3);
readable.pipe(ws3);

await new Promise(resolve => ws3.on('close', resolve));
const readBack = fs.readFileSync(testPath3, 'utf8');
assert.equal(readBack, srcContent, 'pipe content matches');
assert.equal(ws3.bytesWritten, srcContent.length, 'bytesWritten for pipe');
fs.unlinkSync(testPath3);

// === Write stream with autoClose: false ===

const testPath4 = tmpPath('wstream-noclose.txt');
const ws4 = fs.createWriteStream(testPath4, { autoClose: false });
ws4.write('data');
ws4.end();

await new Promise(resolve => ws4.on('finish', resolve));
// File should still exist and have data
const content4 = fs.readFileSync(testPath4, 'utf8');
assert.equal(content4, 'data', 'content without autoClose');
// Manually close
ws4.close();
await new Promise(resolve => ws4.on('close', resolve));
fs.unlinkSync(testPath4);

// === Write stream error on invalid path ===

let errorCaught = false;
const wsErr = fs.createWriteStream('/nonexistent/dir/file.txt');
wsErr.on('error', () => { errorCaught = true; });

await new Promise(resolve => setTimeout(resolve, 100));
assert.ok(errorCaught, 'error event on invalid path');

// === Multer-like pattern: pipe busboy file stream to WriteStream ===

// Simulate what multer does: create a readable stream (like busboy file stream)
// and pipe it to a WriteStream
const testPath5 = tmpPath('wstream-multer.txt');
const multerLikeContent = 'This simulates a file upload via multer/busboy\n';

// Create a fake "file stream" (simulating busboy's file stream)
const fileStream = new Readable();
fileStream._read = () => {};
fileStream.push(multerLikeContent);
fileStream.push(null);

const outStream = fs.createWriteStream(testPath5);
fileStream.pipe(outStream);

await new Promise(resolve => outStream.on('close', resolve));
const multerContent = fs.readFileSync(testPath5, 'utf8');
assert.equal(multerContent, multerLikeContent, 'multer-like pipe content');
assert.equal(outStream.bytesWritten, multerLikeContent.length, 'multer-like bytesWritten');
fs.unlinkSync(testPath5);

// === Sequential writes then end ===

const testPath6 = tmpPath('wstream-sequential.txt');
const ws6 = fs.createWriteStream(testPath6);

ws6.write('line1\n');
ws6.write('line2\n');
ws6.write('line3\n');
ws6.end();

await new Promise(resolve => ws6.on('close', resolve));
const linesContent = fs.readFileSync(testPath6, 'utf8');
assert.equal(linesContent, 'line1\nline2\nline3\n', 'sequential writes content');
assert.equal(ws6.bytesWritten, 18, 'sequential bytesWritten');
fs.unlinkSync(testPath6);

// Cleanup temp directory
await cleanup();
