import assert from 'tjs:assert';
import * as os from 'node:os';

const cpus = os.cpus();
assert.ok(Array.isArray(cpus));
assert.ok(cpus.length > 0);

const first = cpus[0];
assert.ok(first && typeof first === 'object');
assert.ok(typeof first.model === 'string');
assert.ok(typeof first.speed === 'number');
assert.ok(first.times && typeof first.times === 'object');
assert.ok(typeof first.times.user === 'number');
assert.ok(typeof first.times.nice === 'number');
assert.ok(typeof first.times.sys === 'number');
assert.ok(typeof first.times.idle === 'number');
assert.ok(typeof first.times.irq === 'number');

const total = os.totalmem();
const free = os.freemem();
const uptime = os.uptime();

assert.ok(typeof total === 'number');
assert.ok(typeof free === 'number');
assert.ok(typeof uptime === 'number');
assert.ok(Number.isFinite(total));
assert.ok(Number.isFinite(free));
assert.ok(Number.isFinite(uptime));
assert.ok(total > 0);
assert.ok(free >= 0);
assert.ok(uptime >= 0);
assert.ok(total >= free);
