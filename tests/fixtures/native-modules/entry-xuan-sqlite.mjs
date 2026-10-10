import { Database } from 'sqlite';

const db = new Database(':memory:');
db.exec('CREATE TABLE test (n INTEGER)');
db.exec('INSERT INTO test VALUES (7)');

const stmt = db.prepare('SELECT * FROM test');
const all = stmt.all();
stmt.finalize();
db.close();

console.log(JSON.stringify({
    rows: all.length,
    n: all[0].n,
}));
