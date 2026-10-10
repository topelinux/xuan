#!/usr/bin/env tjs

import assert from 'tjs:assert';
import path from 'tjs:path';

const scriptPath = path.join(import.meta.dirname, 'fixtures', 'shebang-script.js');
const proc = tjs.spawn([tjs.exePath, scriptPath], { stdout: 'pipe', stderr: 'pipe' });
const stdoutStr = await proc.stdout.text();
const status = await proc.wait();

assert.eq(status.exit_status, 0, 'shebang script runs successfully');
assert.eq(stdoutStr.trim(), 'shebang test passed', 'shebang script outputs correctly');
console.log('Shebang tests passed!');
