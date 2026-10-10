import assert from 'tjs:assert';
import path from 'tjs:path';


const scriptPath = path.join(import.meta.dirname, 'helpers', 'import-filename-dirname-main.js');
const args = [ tjs.exePath, 'run', scriptPath ];
const proc = tjs.spawn(args, { stdout: 'pipe', stderr: 'pipe' });
const stdout = await proc.stdout.text();
const status = await proc.wait();

assert.eq(status.exit_status, 0, 'helper script runs');

const data = JSON.parse(stdout.trim());
assert.eq(typeof data.mainFilename, 'string', 'main has __filename');
assert.eq(typeof data.mainDirname, 'string', 'main has __dirname');
assert.eq(typeof data.childFilename, 'string', 'imported module has __filename');
assert.eq(typeof data.childDirname, 'string', 'imported module has __dirname');
assert.ok(data.mainMatches, 'main dirname matches main filename');
assert.ok(data.childMatches, 'imported dirname matches imported filename');
assert.ok(!data.sameFile, 'imported module gets its own filename');
assert.eq(data.mainBasename, 'import-filename-dirname-main.js', 'main filename is correct');
assert.eq(data.childBasename, 'import-filename-dirname-child.js', 'imported filename is correct');
