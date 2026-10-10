const constants = Object.freeze(
  globalThis.Buffer?.constants ?? {
    MAX_LENGTH: 0xffffffff,
    MAX_STRING_LENGTH: (1 << 30) - 1,
  },
);

function atob(value) {
  return globalThis.atob(value);
}

function btoa(value) {
  return globalThis.btoa(value);
}

function transcode(source, fromEnc, toEnc) {
  if (!(source instanceof Uint8Array)) {
    throw new TypeError("source must be a Buffer or Uint8Array");
  }
  return globalThis.Buffer.from(globalThis.Buffer.from(source).toString(fromEnc), toEnc);
}

// Node.js buffer module exports
const Buffer = globalThis.Buffer;

const INSPECT_MAX_BYTES = 50;
const kMaxLength = typeof Buffer?.constants?.MAX_LENGTH === "number"
  ? Buffer.constants.MAX_LENGTH
  : 0xffffffff;

// SlowBuffer is legacy - alias to Buffer for compatibility
const SlowBuffer = Buffer;

// Blob/File may or may not exist on globalThis
const Blob = globalThis.Blob || undefined;
const File = globalThis.File || undefined;

const defaultExport = {
  Buffer,
  atob,
  btoa,
  transcode,
  constants,
  INSPECT_MAX_BYTES,
  kMaxLength,
  SlowBuffer,
  Blob,
  File,
};

export { Buffer, atob, btoa, transcode, constants, INSPECT_MAX_BYTES, kMaxLength, SlowBuffer, Blob, File };
export default defaultExport;
