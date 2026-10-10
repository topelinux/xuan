import assert from 'tjs:assert';
const { Database } = require('sqlite');

const DB_DIR = '/tmp/xuan_test';
const DB_PATH = `${DB_DIR}/sqlite.db`;

await tjs.makeDir(DB_DIR, { recursive: true });

const db = new Database(DB_PATH, { create: false, readOnly: true });

assert.throws(() => {
    db.prepare('INSERT INTO items (name, n, data, score) VALUES(?, ?, ?, ?)').run('ro', 0, null, 0);
}, Error);

const rows = db.prepare('SELECT COUNT(*) AS c FROM items').all();
assert.ok(rows[0].c >= 0);

console.log(JSON.stringify({ dbPath: DB_PATH, readOnlyWriteBlocked: true, rowCount: rows[0].c }));
