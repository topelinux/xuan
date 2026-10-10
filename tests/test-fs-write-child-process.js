import assert from 'tjs:assert';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

const decoder = new TextDecoder();

const tmpDir = await tjs.makeTempDir('test-fs-write-child-process-XXXXXX');
const logFile = path.join(tmpDir, 'app.log');
const fd = fs.openSync(logFile, 'w');

try {
    const proc = spawn('sh', ['-c', 'i=0; while [ "$i" -lt 5 ]; do echo "line-$i"; i=$((i+1)); sleep 0.1; done']);

    let stdout = '';
    let writeQueue = Promise.resolve();

    proc.stdout.on('data', (chunk) => {
        stdout += decoder.decode(chunk, { stream: true });
        writeQueue = writeQueue.then(() => new Promise((resolve, reject) => {
            fs.write(fd, chunk, 0, chunk.byteLength, null, (err, written, buffer) => {
                if (err) {
                    reject(err);
                    return;
                }
                assert.eq(written, chunk.byteLength, 'writes full chunk');
                assert.eq(buffer, chunk, 'callback returns original buffer');
                resolve();
            });
        }));
    });

    const status = await proc.wait();
    stdout += decoder.decode();
    await writeQueue;

    assert.eq(status.exit_status, 0, 'child exits successfully');
    assert.ok(stdout.includes('line-0'), 'stdout captures first line');
    assert.ok(stdout.includes('line-4'), 'stdout captures last line');

    fs.closeSync(fd);
    const fileText = fs.readFileSync(logFile, 'utf8');
    assert.eq(fileText, stdout, 'async fs.write preserves streamed child output');
} finally {
    try {
        fs.closeSync(fd);
    } catch {
        // ignore double close
    }
    try {
        fs.unlinkSync(logFile);
    } catch {
        // ignore cleanup races
    }
    await tjs.remove(tmpDir);
}
