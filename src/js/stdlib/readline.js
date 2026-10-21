import EventEmitter from 'tjs:events';
import process from 'tjs:process';

class Interface extends EventEmitter {
    constructor(options = {}) {
        super();

        this.input = options.input ?? process.stdin;
        this.output = options.output ?? process.stdout;
        this.prompt = options.prompt ?? '> ';
        this.historySize = options.historySize ?? 30;
        this.completion = options.completer;
        this.terminal = options.terminal ?? !!(this.output && this.output.isTerminal);
        this._line = '';
        this._history = [];
        this._historyIndex = -1;
        this._reader = null;
        this._lineCallback = null;
        this._reading = false;
        this._closed = false;
        this._paused = false;
        this._skipLF = false;
    }

    // Node's readline starts consuming input as soon as a 'line' listener is
    // registered (or question() is called), so mirror that here.
    on(event, listener) {
        const result = super.on(event, listener);

        if (event === 'line') {
            this._startReading();
        }

        return result;
    }

    addListener(event, listener) {
        return this.on(event, listener);
    }

    once(event, listener) {
        const result = super.once(event, listener);

        if (event === 'line') {
            this._startReading();
        }

        return result;
    }

    _startReading() {
        if (this._closed || this._reading || this._paused) {
            return;
        }

        void this._readFromStdin();
    }

    _emitLine(line) {
        this.emit('line', line);

        if (this._lineCallback) {
            const callback = this._lineCallback;

            this._lineCallback = null;
            callback(line);
        }
    }

    _finish() {
        if (this._closed) {
            return;
        }

        if (this._line.length > 0) {
            const line = this._line;

            this._line = '';
            this._emitLine(line);

            if (this._closed) {
                return;
            }
        }

        this._closed = true;
        this._lineCallback = null;

        // process.stdin is a plain ReadableStream in xuan; mirror the Node
        // stream 'end' event so `process.stdin.on('end', ...)` keeps working.
        if (this.input && typeof this.input.emit === 'function') {
            this.input.emit('end');
        }

        this.emit('close');
    }

    _consume(value) {
        const chunk = typeof value === 'string' ? value : new TextDecoder().decode(value);

        for (const c of chunk) {
            if (this._skipLF) {
                this._skipLF = false;

                if (c === '\n') {
                    continue;
                }
            }

            if (c === '\n' || c === '\r') {
                if (c === '\r') {
                    this._skipLF = true;
                }

                const line = this._line;

                this._line = '';
                this._emitLine(line);
            } else if (c === '\u007f' || c === '\b') {
                if (this._line.length > 0) {
                    this._line = this._line.slice(0, -1);

                    if (this.terminal && this.output && typeof this.output.write === 'function') {
                        this.output.write('\b \b');
                    }
                }
            } else if (c >= ' ') {
                this._line += c;

                if (this.terminal && this.output && typeof this.output.write === 'function') {
                    this.output.write(c);
                }
            }

            if (this._closed) {
                return;
            }
        }
    }

    async _readFromStdin() {
        if (this._reading) {
            return;
        }

        this._reading = true;

        try {
            if (!this._reader) {
                if (!this.input || typeof this.input.getReader !== 'function') {
                    return;
                }

                this._reader = this.input.getReader();
            }

            while (!this._paused && !this._closed) {
                const result = await this._reader.read();

                if (!result) {
                    break;
                }

                const { done, value } = result;

                if (done) {
                    this._finish();
                    break;
                }

                this._consume(value);
            }
        } catch (e) {
            // After close() the pending read() is intentionally rejected when
            // the reader lock is released; that is not an error worth reporting.
            if (!this._closed) {
                console.error('stdin read error:', e);
            }
        } finally {
            this._reading = false;
        }
    }

    prompt() {
        if (this.output && typeof this.output.write === 'function') {
            this.output.write(this.prompt);
        }
    }

    setPrompt(prompt) {
        this.prompt = prompt;
    }

    getPrompt() {
        return this.prompt;
    }

    question(query, options, callback) {
        if (typeof options === 'function') {
            callback = options;
        }

        if (this._closed) {
            throw new Error('readline was closed');
        }

        if (this.output && typeof this.output.write === 'function') {
            this.output.write(query);
        }

        this._line = '';

        const answer = new Promise(resolve => {
            this._lineCallback = line => {
                if (typeof callback === 'function') {
                    callback(line);
                }

                resolve(line);
            };
        });

        this._startReading();

        if (typeof callback === 'function') {
            return undefined;
        }

        return answer;
    }

    pause() {
        if (this._paused) {
            return;
        }

        this._paused = true;
        this.emit('pause');
    }

    resume() {
        if (!this._paused) {
            return;
        }

        this._paused = false;
        this.emit('resume');
        this._startReading();
    }

    close() {
        if (this._closed) {
            return;
        }

        this._closed = true;
        this._lineCallback = null;

        if (this._reader) {
            try {
                this._reader.releaseLock();
            } catch {
                // Releasing the lock may reject a pending read(); ignore it.
            }

            this._reader = null;
        }

        this.emit('close');
    }

    write(data) {
        if (this.output && typeof this.output.write === 'function') {
            this.output.write(data);
        }
    }
}

function createInterface(options) {
    return new Interface(options);
}

function cursorTo(_stream, _destRow) {
}

function moveCursor(_stream, _dx, _dy) {
}

function clearLine(_stream, _dir) {
}

export {
    Interface,
    createInterface,
    cursorTo,
    moveCursor,
    clearLine,
};

export default {
    Interface,
    createInterface,
    cursorTo,
    moveCursor,
    clearLine,
};
