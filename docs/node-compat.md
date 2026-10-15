# Node.js compatibility

xuan aims for practical Node.js compatibility on edge/IoT workloads, not
byte-for-byte parity with Node. This document tracks what is actually
implemented, what is partial, and what is missing, so you can judge whether
your code will run **before** you ship it.

> **Last verified:** 2026-10-14, commit `204c350` (v1.3.0).
> Generated from the source (`src/builtins.c`, `src/js/stdlib/`). "Present"
> means the common surface is implemented; it is not an exhaustive test of
> every method against Node's suite. If you hit something missing, please
> open an issue.

## How the aliasing works

Node builtins are exposed as `tjs:*` modules and aliased so `node:<name>` and
the bare `<name>` both resolve to them (`src/builtins.c`). Both styles work:

```js
import { readFile } from 'node:fs/promises';
const os = require('node:os');
const path = require('path');          // bare name also works
```

Legend: ✅ present · 🟡 partial (known gaps listed) · ❌ not available.

## Implemented modules

| Module | Status | Notes / known gaps |
|--------|:------:|--------------------|
| `assert` | ✅ | Adapted from zora.js's assertion library. |
| `buffer` | ✅ | `Buffer` implemented in `mod_buffer`. |
| `events` | ✅ | `EventEmitter`, `on`/`once`, max-listeners helpers. |
| `fs` | ✅ | Broad callback + sync API. Known gap: `fs.rmSync(..., { recursive: true })` not supported yet. |
| `fs/promises` | ✅ | Main surface. `access()` is approximated via `stat()`; some exotic options unimplemented. |
| `path` | ✅ | Includes `posix` / `win32`. |
| `process` | ✅ | Also installed as the `process` global. |
| `querystring` | ✅ | |
| `stream` | ✅ | `Readable`/`Writable`/`Duplex`/`Transform`/`PassThrough`, `pipeline`, `finished`. |
| `string_decoder` | ✅ | `StringDecoder`. |
| `url` | ✅ | `URL`, `URLSearchParams`, `URLPattern`, `pathToFileURL`, `fileURLToPath`. |
| `util` | ✅ | `format`, `inspect`, `promisify`, `inherits`, `deprecate`, `types`, … |
| `dgram` | ✅ | `createSocket`, `Socket`. |
| `http` | ✅ | Client **and** server. |
| `https` | ✅ | Requires a TLS-enabled build. |
| `net` | ✅ | `connect`/`createConnection`/`createServer`, `isIP`/`isIPv4`/`isIPv6`. |
| `readline` | ✅ | Basic line interface (`createInterface`, `question`, `close`). |
| `tls` | ✅ | `connect`, `TLSSocket`, `createSecureContext`. |
| `tty` | ✅ | `isatty`, read/write streams. |
| `wasi` | ✅ | WASI preview support. |
| `child_process` | 🟡 | `spawn`, `spawnSync`, `exec`, `execSync`. Missing: `execFile`, `fork`. |
| `crypto` | 🟡 | Hashing (`createHash`), HMAC (`createHmac`), `randomBytes`/`randomFill(Sync)`, `timingSafeEqual`, `getHashes`. Missing: ciphers, `createSign`/`createVerify`, key generation. `randomUUID` is available via the Web Crypto global. |
| `dns` | 🟡 | `lookup` and `Resolver.resolve4`/`resolve6` are real. `resolveTxt` is **not implemented** and throws `ERR_NOT_IMPLEMENTED` (it never returns an empty result, so it can't be mistaken for a successful lookup). |
| `http2` | 🟡 | **Client only.** `createServer()` / `createSecureServer()` throw `not implemented`. Only present when built with HTTP/2. |
| `os` | 🟡 | `cpus`, `freemem`, `totalmem`, `uptime`, `networkInterfaces`. Missing: `hostname`, `platform`, `arch`, `release`, `type`, `homedir`, `tmpdir`, `loadavg`, `endianness`, `userInfo`, `constants` (some equivalents live on `process`). |
| `zlib` | 🟡 | `gunzip`/`gzip` paths present. `deflateRaw` / `inflateRaw` throw `zlib is not supported in this build`. |

### Non-Node built-in modules

These have no Node equivalent and are specific to xuan:

| Module | Purpose |
|--------|---------|
| `tjs:ffi` | Call C library functions directly (reuse existing C libs). |
| `tjs:mqtt` (`mqtt`, `mini-mqtt-client`) | MQTT client. |
| `tjs:serial` (`serialport`) | Serial port access. |
| `tjs:ws` (`ws`) | WebSocket. **Client only**; server mode not supported yet. |
| `tjs:npm` | Built-in npm-compatible package manager. |
| `tjs:sqlite` | SQLite (optional; see build notes). |
| `tjs:hashing`, `tjs:uuid`, `tjs:ipaddr`, `tjs:getopts`, `tjs:posix-socket` | Small utilities. |

## Not available

There is no `node:` alias for these, and no built-in equivalent:

| Node module | Notes |
|-------------|-------|
| `worker_threads` | A **Web Worker** (`Worker`) global exists, but the `worker_threads` API does not. |
| `vm` | No script sandboxing module. |
| `cluster` | Not implemented. |
| `async_hooks` | Not implemented. |
| `perf_hooks` | Not implemented. |
| `v8` | Not implemented (QuickJS, not V8). |
| `inspector` | Not implemented. |
| `module` | No custom ESM loader hooks / `register()`. |
| `diagnostics_channel` | Not implemented. |
| `repl` | A REPL binary exists, but no `node:repl` module. |

## Global / platform gaps

- **No `Intl`.** There is no ICU. Locale-aware formatting (`Intl.DateTimeFormat`,
  `Intl.NumberFormat`, `Intl.Collator`, …) is not available; bring a polyfill if
  you need it.
- **No native C++ addons.** Node addons built with node-gyp (`.node` files) do
  not load. The supported ways to reach native code are:
  1. **FFI** (`tjs:ffi`) — call functions in existing C shared libraries.
  2. **xuan native modules** — loadable `.so`/`.dylib`/`.dll` (see
     `xuan_modules/`).
  3. **WebAssembly** — via the WebAssembly runtime module.

## Why the footprint is small

The size and cold-start numbers come from deliberate trade-offs, not from
"compatibility we happened to skip":

- **QuickJS (interpreter), not V8 (JIT).** Much smaller binary and faster
  startup; lower peak throughput on hot loops.
- **No ICU / `Intl`** (see above) — one of the largest single cuts.
- **TLS via mbedTLS**, not OpenSSL.
- **Optional subsystems** are compile-time options (SQLite, WebAssembly/WAMR,
  LVGL, native modules); several are provided as loadable native modules rather
  than compiled into the binary.
- **Tiered startup.** Minimal polyfills load for `-e`/`--help`/`--version`;
  `compat` and `heavy` (e.g. `CompressionStream`, `WebSocketStream`) load only
  when used, and modules are resolved lazily.

## Reporting a gap

If your package needs something in the 🟡 or ❌ tables, please open an issue
with the module, the API, and a minimal snippet. The roadmap is driven by real
usage rather than guesses.
