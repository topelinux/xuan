import assert from 'tjs:assert';
const { Database } = require('sqlite');

const DB_PATH = '/tmp/xuan_test/sqlite.db';

const db = new Database(DB_PATH, { create: true, readOnly: false });

db.exec(`CREATE TABLE IF NOT EXISTS items (name TEXT PRIMARY KEY, n INTEGER)`);
db.exec(`DELETE FROM items`);
db.exec(`INSERT INTO items (name, n) VALUES ('alpha', 1), ('beta', 2), ('gamma', 0)`);

const byArray = db.prepare('SELECT name, n FROM items WHERE n >= ? ORDER BY n').all([1]);
assert.ok(byArray.length >= 2);

const byNamed = db.prepare('SELECT name, n FROM items WHERE name = $name').all({ $name: 'alpha' });
assert.eq(byNamed.length, 1);
assert.eq(byNamed[0].n, 1);

assert.throws(() => {
    db.prepare('SELECT name FROM items WHERE name = $name').all({ name: 'alpha' });
}, ReferenceError);

console.log(JSON.stringify({
    dbPath: DB_PATH,
    arrayBindingRows: byArray.length,
    namedBindingName: byNamed[0].name,
}));
