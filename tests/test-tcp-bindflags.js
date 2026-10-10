import assert from 'tjs:assert';
import { spawnSync } from 'node:child_process';

const script = `await tjs.listen('tcp', '127.0.0.1', 0, { ipv6Only: true });`;
const ret = spawnSync(process.execPath, [ 'eval', script ], { encoding: 'utf8' });
assert.eq(ret.exit_status ?? ret.status, 1);
assert.ok(ret.stderr.includes('EINVAL'), 'binding with ipv6Only on IPv4 fails');
