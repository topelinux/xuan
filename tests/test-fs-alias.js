import assert from 'tjs:assert';
import fs from 'fs';
import nodeFs from 'node:fs';
import path from 'path';
import { appendFileSync, closeSync, openSync, readFileSync, readSync, renameSync, rmSync, unlinkSync, writeFileSync, writeSync } from 'fs';

const tmpDir = await tjs.makeTempDir('test-fs-alias-XXXXXX');
const file = path.join(tmpDir, 'hello.txt');

await fs.promises.writeFile(file, 'hello');
const text = await fs.promises.readFile(file, 'utf8');
assert.eq(text, 'hello');

await new Promise((resolve, reject) => {
    fs.writeFile(file, 'world', (err) => {
        if (err) reject(err);
        else resolve();
    });
});

const text2 = await new Promise((resolve, reject) => {
    fs.readFile(file, 'utf8', (err, data) => {
        if (err) reject(err);
        else resolve(data);
    });
});
assert.eq(text2, 'world');

assert.eq(fs.existsSync(file), true);
assert.eq(nodeFs.existsSync(file), true);
assert.eq(fs.statSync(file).isFile(), true);
writeFileSync(file, 'sync');
assert.eq((await fs.promises.readFile(file, 'utf8')), 'sync');
writeFileSync(file, '');
assert.eq(fs.statSync(file).size, 0);
appendFileSync(file, '+append');
assert.eq(readFileSync(file, 'utf8'), '+append');
const fh = await fs.promises.open(file, 'r');
closeSync(fh.fileno());
const fd = openSync(file, 'r');
assert.eq(typeof fd, 'number');
const readBuf = new Uint8Array(4);
assert.eq(readSync(fd, readBuf, 0, 4, 0), 4);
assert.eq(new TextDecoder().decode(readBuf), '+app');
closeSync(fd);

const fdw = openSync(file, 'w');
assert.eq(writeSync(fdw, new Uint8Array(0), 0, 0, 0), 0);
const toWrite = new TextEncoder().encode('WXYZ');
assert.eq(writeSync(fdw, toWrite, 0, 4, 0), 4);
closeSync(fdw);
assert.eq(readFileSync(file, 'utf8'), 'WXYZ');

const fdAsyncString = openSync(file, 'w');
const asyncStringResult = await new Promise((resolve, reject) => {
    fs.write(fdAsyncString, 'hello', 0, (err, written, str) => {
        if (err) {
            reject(err);
            return;
        }
        resolve({ written, str });
    });
});
closeSync(fdAsyncString);
assert.eq(asyncStringResult.written, 5);
assert.eq(asyncStringResult.str, 'hello');
assert.eq(readFileSync(file, 'utf8'), 'hello');

const fdAsyncBuffer = openSync(file, 'w');
const asyncBuf = new TextEncoder().encode('abcd');
const asyncEmptyResult = await new Promise((resolve, reject) => {
    fs.write(fdAsyncBuffer, new Uint8Array(0), 0, 0, 0, (err, written, buf) => {
        if (err) {
            reject(err);
            return;
        }
        resolve({ written, buf });
    });
});
assert.eq(asyncEmptyResult.written, 0);
const asyncBufferResult = await new Promise((resolve, reject) => {
    fs.write(fdAsyncBuffer, asyncBuf, 1, 2, 0, (err, written, buf) => {
        if (err) {
            reject(err);
            return;
        }
        resolve({ written, buf });
    });
});
closeSync(fdAsyncBuffer);
assert.eq(asyncBufferResult.written, 2);
assert.eq(asyncBufferResult.buf, asyncBuf);
assert.eq(readFileSync(file, 'utf8'), 'bc');

const moved = path.join(tmpDir, 'moved.txt');
renameSync(file, moved);
assert.eq(fs.existsSync(moved), true);
unlinkSync(moved);
assert.eq(fs.existsSync(moved), false);

const emptyDir = path.join(tmpDir, 'empty-dir');
fs.mkdirSync(emptyDir);
rmSync(emptyDir);
assert.eq(fs.existsSync(emptyDir), false);

assert.eq(typeof nodeFs.promises.readFile, 'function');
const names = await fs.promises.readdir(tmpDir);
assert.ok(names.length === 0);
assert.eq(fs.readdirSync(tmpDir).length, 0);

const subdir = path.join(tmpDir, 'subdir');
const listedFile = path.join(tmpDir, 'listed.txt');
fs.mkdirSync(subdir);
writeFileSync(listedFile, 'x');

const syncNames = fs.readdirSync(tmpDir);
assert.ok(syncNames.includes('subdir'));
assert.ok(syncNames.includes('listed.txt'));

const dirents = fs.readdirSync(tmpDir, { withFileTypes: true });
const byName = new Map(dirents.map(item => [ item.name, item ]));
assert.ok(byName.get('subdir').isDirectory);
assert.ok(byName.get('listed.txt').isFile);

await tjs.remove(tmpDir);
