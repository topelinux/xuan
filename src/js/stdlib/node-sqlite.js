// Synchronous subset of node:sqlite. Each execution finalizes its native
// statement, including on errors, so short-lived prepare().get()/all() callers
// do not keep WAL readers or native statements alive until garbage collection.
const sqlite = globalThis[Symbol.for('tjs.internal.core')].sqlite3;

class DatabaseSync {
    constructor(filename, options = {}) {
        if (!sqlite) {
            throw new Error('SQLite3 support is disabled in this build');
        }

        if (options.open === false) {
            throw new Error('Deferred open is not supported');
        }

        const flags = options.readOnly
            ? sqlite.SQLITE_OPEN_READONLY
            : sqlite.SQLITE_OPEN_READWRITE | sqlite.SQLITE_OPEN_CREATE;

        this.handle = sqlite.open(String(filename), flags);

        if (options.enableForeignKeyConstraints !== false) {
            this.exec('PRAGMA foreign_keys=ON');
        }
    }

    check() {
        if (!this.handle) {
            throw new Error('Database is not open');
        }
    }

    exec(sql) {
        this.check();
        sqlite.exec(this.handle, String(sql));
    }

    prepare(sql) {
        this.check();

        return new StatementSync(this, String(sql));
    }

    close() {
        this.check();
        sqlite.close(this.handle);
        this.handle = null;
    }
}

class StatementSync {
    constructor(db, sql) {
        this.db = db;
        this.sql = sql;

        // Match Node: syntax errors occur during prepare, even before execution.
        const statement = sqlite.prepare(db.handle, sql);

        sqlite.stmt_finalize(statement);
    }

    execute(args, run) {
        this.db.check();

        const statement = sqlite.prepare(this.db.handle, this.sql);

        try {
            if (!run) {
                return sqlite.stmt_all(statement, args);
            }

            sqlite.stmt_run(statement, args);
        } finally {
            sqlite.stmt_finalize(statement);
        }

        const info = sqlite.prepare(
            this.db.handle,
            'SELECT changes() AS changes, last_insert_rowid() AS lastInsertRowid'
        );

        try {
            return sqlite.stmt_all(info, [])[0];
        } finally {
            sqlite.stmt_finalize(info);
        }
    }

    all(...args) {
        return this.execute(args, false);
    }

    get(...args) {
        return this.execute(args, false)[0];
    }

    run(...args) {
        return this.execute(args, true);
    }
}

export { DatabaseSync, StatementSync };
export default { DatabaseSync, StatementSync };
