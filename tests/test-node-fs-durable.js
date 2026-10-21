import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
function check(ok, message) { if (!ok) throw new Error(message); }
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xuan-durable-'));
try {
    const pending = path.join(dir, 'state.tmp'), final = path.join(dir, 'state.json');
    const fd = fs.openSync(pending, 'wx', 0o600);
    fs.writeFileSync(fd, '{"counter":4294967295}\n');
    fs.fsyncSync(fd); fs.closeSync(fd); fs.renameSync(pending, final);
    const directory = fs.openSync(dir, 'r'); fs.fsyncSync(directory); fs.closeSync(directory);
    check(fs.readFileSync(final, 'utf8') === '{"counter":4294967295}\n', 'fd write must target descriptor');
    fs.chmodSync(final, 0o600); check((fs.statSync(final).mode & 0o777) === 0o600, 'private state permissions');
    let failed = false; try { fs.openSync(final, 'wx', 0o600); } catch (_) { failed = true; }
    check(failed, 'exclusive creation must fail for existing state');
    fs.mkdirSync(path.join(dir,'nested')); fs.writeFileSync(path.join(dir,'nested','data'), 'ok');
    console.log('Node durable state: exclusive create, descriptor write, file/directory fsync, rename and chmod passed');
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
