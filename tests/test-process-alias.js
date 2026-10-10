import assert from 'tjs:assert';
import os from 'node:os';
import process from 'process';
import nodeProcess from 'node:process';

assert.eq(typeof process.cwd, 'function');
assert.eq(typeof process.nextTick, 'function');
assert.eq(typeof process.hrtime, 'function');
assert.eq(typeof process.hrtime.bigint, 'function');
assert.eq(typeof process.pid, 'number');
assert.eq(typeof process.ppid, 'number');
assert.eq(Array.isArray(process.argv), true);
assert.eq(typeof process.env, 'object');
assert.eq(typeof process.version, 'string');
assert.eq(typeof process.uptime, 'function');
assert.eq(typeof nodeProcess.cwd, 'function');

const envKey = 'XUAN_PROCESS_ENV_ENUMERATION_TEST';
delete process.env[envKey];
process.env[envKey] = 'hello';

try {
    const descriptor = Object.getOwnPropertyDescriptor(process.env, envKey);
    assert.eq(descriptor.value, 'hello');
    assert.eq(descriptor.enumerable, true);
    assert.eq(descriptor.writable, true);
    assert.eq(descriptor.configurable, true);
    assert.eq(Reflect.ownKeys(process.env).includes(envKey), true);
    assert.eq(Object.keys(process.env).includes(envKey), true);
    assert.eq(Object.fromEntries(Object.entries(process.env))[envKey], 'hello');
    assert.eq({ ...process.env }[envKey], 'hello');
    assert.eq(envKey in process.env, true);
} finally {
    delete process.env[envKey];
}

assert.eq(Object.getOwnPropertyDescriptor(process.env, envKey), undefined);
assert.eq(envKey in process.env, false);

const cwd = process.cwd();
assert.eq(cwd, tjs.cwd);

const hr = process.hrtime();
assert.eq(Array.isArray(hr), true);
assert.eq(hr.length, 2);

const processUptimeStart = process.uptime();
const osUptime = os.uptime();
assert.ok(Number.isFinite(processUptimeStart));
assert.ok(processUptimeStart >= 0);
assert.ok(processUptimeStart < osUptime);

await new Promise((resolve) => setTimeout(resolve, 20));
assert.ok(process.uptime() >= processUptimeStart);

await new Promise((resolve, reject) => {
    process.nextTick((a, b) => {
        try {
            assert.eq(a + b, 3);
            resolve();
        } catch (e) {
            reject(e);
        }
    }, 1, 2);
});

const requiredProcessNs = require('process');
const requiredProcess = requiredProcessNs.default ?? requiredProcessNs;
assert.eq(typeof requiredProcess.cwd, 'function');
assert.eq(requiredProcess.cwd(), tjs.cwd);
