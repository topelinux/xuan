import assert from 'tjs:assert';
import fs from 'fs';
import pathModule from 'tjs:path';

const tmpDir = `/tmp/test_mkdir_sync_${tjs.pid}`;

await fs.mkdirSync(tmpDir, { recursive: true });
assert.ok(fs.existsSync(tmpDir), 'base dir created');

const nestedPath = pathModule.join(tmpDir, 'a', 'b', 'c');
await fs.mkdirSync(nestedPath, { recursive: true });
assert.ok(fs.existsSync(nestedPath), 'nested dir created');

const anotherNested = `${tmpDir}/x/y/z`;
fs.mkdirSync(anotherNested, { recursive: true });
assert.ok(fs.existsSync(anotherNested), 'string path nested dir created');

fs.mkdirSync(`${tmpDir}/existing`, { recursive: true });
assert.ok(fs.existsSync(`${tmpDir}/existing`), 'existing dir still accessible');

console.log('fs.mkdirSync recursive tests passed!');
