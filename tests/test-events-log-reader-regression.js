import assert from 'tjs:assert';
import path from 'tjs:path';

import { spawn } from 'node:child_process';

const EventEmitter = require('events');
const util = require('util');
const LogBuffer = require(path.join(import.meta.dirname, 'helpers', 'log-buffer.cjs'));

function LogReader() {
    EventEmitter.call(this);

    const self = this;
    this._started = false;

    this.on('data', function(data) {
        self.emit('log', Buffer.from(data).toString());
    });
}

util.inherits(LogReader, EventEmitter);

LogReader.prototype.start = function() {
    this._started = true;
};

LogReader.prototype.write = function(chunk, _encoding, callback) {
    if (this._started) {
        this.emit('data', chunk);
    }

    if (callback) {
        callback();
    }
};

const reader = new LogReader();
const buffer = new LogBuffer(8);
const seen = [];

reader.on('log', (text) => {
    buffer.append(text);
});

reader.on('log', (text) => {
    seen.push(text);
});

reader.start();

const proc = spawn('sh', ['-c', 'i=0; while [ "$i" -lt 12 ]; do echo "log-$i"; i=$((i+1)); sleep 0.05; done']);

proc.stdout.on('data', (chunk) => {
    reader.write(chunk);
});

const status = await proc.wait();

assert.eq(status.exit_status, 0, 'child exits successfully');
assert.ok(seen.length >= 12, 'all log events are delivered');

const latest = buffer.fetch({ latest: 3 });
assert.eq(latest.length, 3, 'ring buffer returns latest entries');
assert.ok(latest[0][1].includes('log-9'), 'latest window keeps recent entries');
assert.ok(latest[2][1].includes('log-11'), 'latest window keeps newest entry');

const after = latest[0][0] - 1;
const fetched = buffer.fetch({ after });
assert.ok(fetched.length >= 3, 'fetch(after) returns newer entries');
