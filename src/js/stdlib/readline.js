import process from 'tjs:process';

class Interface {
    constructor(options) {
        this.input = options.input;
        this.output = options.output;
        this.prompt = options.prompt ?? '> ';
        this.historySize = options.historySize ?? 30;
        this.completion = options.completer;
        this._line = '';
        this._history = [];
        this._historyIndex = -1;
        this._reader = null;
        this._lineCallback = null;
        this._reading = false;
    }

    async _readFromStdin() {
        if (this._reading) return;
        this._reading = true;

        try {
            if (!this._reader) {
                this._reader = process.stdin.getReader();
            }

            while (true) {
                const result = await this._reader.read();
                if (!result) break;

                const { done, value } = result;
                if (done) break;

                const chunk = typeof value === 'string' ? value : new TextDecoder().decode(value);
                for (const c of chunk) {
                    if (c === '\n' || c === '\r') {
                        if (this._lineCallback) {
                            const line = this._line;
                            this._line = '';
                            const cb = this._lineCallback;
                            this._lineCallback = null;
                            cb(line);
                        }
                    } else if (c === '\u007f' || c === '\b') {
                        if (this._line.length > 0) {
                            this._line = this._line.slice(0, -1);
                            if (this.output && typeof this.output.write === 'function') {
                                this.output.write('\b \b');
                            }
                        }
                    } else if (c >= ' ') {
                        this._line += c;
                        if (this.output && typeof this.output.write === 'function') {
                            this.output.write(c);
                        }
                    }
                }
            }
        } catch (e) {
            console.error('stdin read error:', e);
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

    question(query, callback) {
        if (this.output && typeof this.output.write === 'function') {
            this.output.write(query);
        }

        this._line = '';
        this._lineCallback = callback;
        this._readFromStdin();
    }

    close() {
        this._lineCallback = null;
        if (this._reader) {
            this._reader.releaseLock();
            this._reader = null;
        }
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

function cursorTo(stream, destRow) {
}

function moveCursor(stream, dx, dy) {
}

function clearLine(stream, dir) {
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
