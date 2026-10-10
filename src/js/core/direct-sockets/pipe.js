import {
    core,
    kHandle,
    kOpened,
    silentClose,
    BaseStreamSocket,
    BaseStreamServerSocket
} from './utils.js';


function isUnixPipePath(path) {
    return core.platform !== 'windows' && typeof path === 'string' && path[0] !== '\0';
}

async function unlinkPipePath(path) {
    if (!isUnixPipePath(path)) {
        return;
    }

    try {
        await core.unlink(path);
    } catch (error) {
        if (error?.code !== 'ENOENT') {
            throw error;
        }
    }
}

function formatPipeAddress(localAddr, remoteAddr) {
    return {
        localAddress: localAddr,
        remoteAddress: remoteAddr,
    };
}


export class PipeSocket extends BaseStreamSocket {
    constructor(path) {
        if (typeof path !== 'string') {
            throw new TypeError('path must be a string');
        }

        super(new core.Pipe());

        this[kOpened] = this._setup(path);
    }

    async _setup(path) {
        const handle = this[kHandle];

        try {
            await this._connect(path);

            const localAddr = handle.getsockname();
            const remoteAddr = handle.getpeername();

            return {
                ...this._buildOpenedInfo(),
                ...formatPipeAddress(localAddr, remoteAddr),
            };
        } catch (error) {
            silentClose(handle);
            this._closedReject(error);
            throw error;
        }
    }
}


export class PipeServerSocket extends BaseStreamServerSocket {
    constructor(path, options = {}) {
        if (typeof path !== 'string') {
            throw new TypeError('path must be a string');
        }

        super(new core.Pipe());

        this._pipePath = isUnixPipePath(path) ? path : null;
        this._closing = false;
        this[kOpened] = this._bind(path, options);
    }

    async _bind(path, options) {
        const handle = this[kHandle];

        try {
            handle.bind(path);
            handle.listen(options.backlog);
        } catch (error) {
            silentClose(handle);
            this._closedReject(error);
            throw error;
        }

        try {
            const createSocket = clientHandle =>
                this._createAcceptedSocket(PipeSocket.prototype, clientHandle, formatPipeAddress);

            const readable = this._createAcceptStream(createSocket);

            const localAddr = handle.getsockname();

            return {
                readable,
                localAddress: localAddr,
            };
        } catch (error) {
            silentClose(handle);
            this._closedReject(error);
            throw error;
        }
    }

    close() {
        if (this._closing) {
            return;
        }

        this._closing = true;
        silentClose(this[kHandle]);

        const path = this._pipePath;
        this._pipePath = null;

        Promise.resolve(unlinkPipePath(path)).finally(() => {
            this._closedResolve();
        });
    }
}
