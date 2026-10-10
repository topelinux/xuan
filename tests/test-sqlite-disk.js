import assert from 'tjs:assert';
const { Database } = require('sqlite');

const DB_DIR = '/tmp/xuan_test';
const DB_PATH = `${DB_DIR}/sqlite.db`;

async function resetDiskDb() {
    await tjs.makeDir(DB_DIR, { recursive: true });
    try {
        await tjs.remove(DB_PATH);
    } catch (_) {
        // Ignore if file doesn't exist.
    }
}

function createAndWrite() {
    const db = new Database(DB_PATH, { create: true, readOnly: false });

    db.exec('PRAGMA journal_mode = WAL;');
    db.exec('CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, n INTEGER, data BLOB, score FLOAT)');

    const ins = db.prepare('INSERT INTO items (name, n, data, score) VALUES($name, $n, $data, $score)');
    ins.run({ $name: 'alpha', $n: 1, $data: new Uint8Array([1, 2, 3]), $score: 1.5 });
    ins.run({ $name: 'beta', $n: 2, $data: new Uint8Array([9, 8]), $score: 2.5 });
    ins.finalize();

    db.close();
}

function reopenAndCheck() {
    const db = new Database(DB_PATH, { readOnly: true, create: false });
    const rows = db.prepare('SELECT name, n, data, score FROM items ORDER BY id').all();

    assert.eq(rows.length, 2);
    assert.eq(rows[0].name, 'alpha');
    assert.eq(rows[0].n, 1);
    assert.eq(rows[0].data[0], 1);
    assert.eq(rows[0].score, 1.5);
    assert.eq(rows[1].name, 'beta');
    assert.eq(rows[1].data[1], 8);

    db.close();
}

function transactionRollbackCheck() {
    const db = new Database(DB_PATH, { create: true, readOnly: false });
    const ins = db.prepare('INSERT INTO items (name, n, data, score) VALUES(?, ?, ?, ?)');
    const tx = db.transaction(() => {
        ins.run('rolled-back', 999, new Uint8Array([7]), 9.9);
        throw new Error('rollback check');
    });

    assert.throws(() => tx(), Error);

    const rows = db.prepare("SELECT name FROM items WHERE name = 'rolled-back'").all();
    assert.eq(rows.length, 0);

    ins.finalize();
}

await resetDiskDb();
createAndWrite();
reopenAndCheck();
transactionRollbackCheck();

const st = await tjs.stat(DB_PATH);
assert.ok(st.isFile, 'sqlite db file should exist on disk');

const finalDb = new Database(DB_PATH, { readOnly: true, create: false });
const total = finalDb.prepare('SELECT COUNT(*) AS c FROM items').all()[0].c;
finalDb.close();

console.log(JSON.stringify({ dbPath: DB_PATH, totalRows: total }));
