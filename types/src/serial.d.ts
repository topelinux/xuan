/**
 * Serial port module.
 *
 * @module tjs:serial
 */
declare module 'tjs:serial' {
    export interface Rs485Options {
        enabled?: boolean;
        rtsOnSend?: boolean;
        rtsAfterSend?: boolean;
        delayBeforeSendMs?: number;
        delayAfterSendMs?: number;
    }

    export interface SerialPortOptions {
        path: string;
        baudRate?: number;
        dataBits?: 5 | 6 | 7 | 8;
        stopBits?: 1 | 2;
        parity?: 'none' | 'even' | 'odd';
        rtscts?: boolean;
        autoOpen?: boolean;
        readSize?: number;
        nativePath?: string;
        rs485?: Rs485Options;
    }

    export class SerialPort {
        constructor(options: SerialPortOptions);

        readonly path: string;
        readonly isOpen: boolean;
        readonly fd: number;

        on(eventName: string, listener: (...args: any[]) => void): this;
        off(eventName: string, listener: (...args: any[]) => void): this;
        once(eventName: string, listener: (...args: any[]) => void): this;

        open(): Promise<this>;
        close(): Promise<void>;
        write(data: string | ArrayBuffer | ArrayBufferView): Promise<number>;
        drain(): Promise<void>;
        flush(queue?: 'both' | 'input' | 'output'): Promise<void>;
        update(options?: Partial<SerialPortOptions>): Promise<void>;
        setRS485(options?: Rs485Options): Promise<void>;
    }

    export default SerialPort;
}
