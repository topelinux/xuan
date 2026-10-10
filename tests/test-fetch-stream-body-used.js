import assert from 'tjs:assert';
import { spawnSync } from 'node:child_process';

// Keep this test network-independent: bodyUsed/read-twice semantics are on Body mixin.
const response = new Response('hello');
await response.text();
assert.ok(response.bodyUsed, 'bodyUsed is true');

const script = `
    const response = new Response('hello');
    await response.text();
    await response.text();
`;
const ret = spawnSync(process.execPath, [ 'eval', script ], { encoding: 'utf8' });
assert.eq(ret.exit_status ?? ret.status, 1);
assert.ok(ret.stderr.includes('Already read'), 'reading body twice throws');
