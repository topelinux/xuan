# xuan

> The invisible runtime — a lightweight JavaScript runtime for the Internet of Things

**English** | [中文](./README.zh-CN.md)

xuan (pronounced "shwen") is a lightweight JavaScript runtime designed for the
Internet of Things. It is built on top of [QuickJS](https://bellard.org/quickjs/)
and [tjs (txiki.js)](https://github.com/saghul/txiki.js/), and inherits their
small footprint:

- **Broad Node.js API compatibility** — run existing npm packages directly
- **Built-in IoT modules** — MQTT, Serial Port (GPIO/I2C/SPI coming soon)
- **FFI (Foreign Function Interface)** — call C library functions directly and reuse existing C libraries
- **Low memory footprint** — significantly smaller than Node.js
- **Compact binary** — roughly 6–7 MB (smaller without SQLite)

Built for embedded and edge devices.

---

## Features

- **⚡ Instant startup** — millisecond cold start, ideal for resource-constrained IoT devices
- **📦 Tiny footprint** — ~6–7 MB binary (smaller without SQLite)
- **💾 Low memory usage** — a fraction of Node.js memory usage
- **🌐 Node.js compatible** — broad API compatibility; run existing npm packages
- **🔌 IoT-ready built-in modules** — native MQTT and Serial Port (GPIO/I2C/SPI coming soon)
- **🛠️ Embedded ready** — supports MIPS, ARM, RISC-V and x86
- **📦 Standalone compilation** — compile JavaScript into a single self-contained executable
- **🧩 ESM + CommonJS** — `import`/`export`, dynamic `import()`, top-level `await` and `require()`

---

## Use cases

- 🏠 **Smart home** — lighting control and sensor data collection
- 🏭 **Industrial IoT** — device monitoring and edge computing
- 🌾 **Agricultural IoT** — environmental monitoring and automated irrigation
- 📡 **Edge gateways** — protocol conversion and data pre-processing
- 🤖 **Edge AI agents** — local LLM-based agents on constrained hardware

---

## Quick start

### Clone and build

```bash
# Clone the project
git clone https://github.com/topelinux/xuan.git
cd xuan

# Initialize submodules
git submodule update --init --recursive --force

# Install JS build dependencies (esbuild, polyfills, etc.)
npm install

# Build
make
```

> `make` uses `npx esbuild` to generate bundles, so run `npm install` first.

> Local changes to the bundled dependencies (`libwebsockets`, `quickjs`, `libuv`,
> `mimalloc`) are stored as patches under `patches/` and are applied automatically
> by `make`. Do not edit files inside `deps/` submodules directly.

### Hello World

```javascript
// main.js
console.log('Hello from xuan!');

// IoT example: read a GPIO pin (coming soon)
// const gpio = require('gpio');
// const sensor = gpio.open(17);
// sensor.read((value) => {
//     console.log('Sensor value:', value);
// });
```

### Run

```bash
./build/xuan run main.js
```

### Compile to a standalone executable (recommended for IoT)

Bundle JavaScript into a single self-contained executable that can be shipped
with no runtime dependencies:

```bash
# Compile a script into a standalone executable
./build/xuan compile main.js myapp

# Run it
./myapp
```

**Benefits:**

- No Node.js required — truly standalone deployment
- Smaller size and faster startup
- Ideal for distributing to resource-constrained IoT devices

### Package management

xuan ships with an npm-compatible package manager:

```bash
# Initialize a project
./build/xuan npm init

# Install a package
./build/xuan npm install <package>

# Uninstall a package
./build/xuan npm uninstall <package>
```

Packages are downloaded from the npm registry into a local `node_modules`
directory and can be `require`d directly.

### Recommended: bundle with esbuild (IoT)

For resource-constrained environments, bundling and minifying your application
with esbuild into a single file reduces size and startup time.

Install esbuild first:

```bash
npm install -g esbuild
```

Then bundle:

```bash
# Bundle and minify
esbuild entry.js --bundle --minify --outfile=bundle.js
```

Run the bundle with xuan:

```bash
./build/xuan run bundle.js
```

> 💡 **xuan compile vs. esbuild**: `xuan compile` embeds your JS into the runtime
> and produces a standalone binary, ideal for direct distribution. esbuild
> reduces the size of the JS payload but still requires the xuan runtime.

### Example: edge AI agent (XuanLing)

xuan can act as the runtime for an edge AI agent, running lightweight LLM
applications on constrained devices:

```bash
# Enter the example directory
cd xuan_apps/xuanling

# Set up the LLM provider
cp config.example.json config.json

# Start an interactive chat
./build/xuan run xuan_apps/xuanling/bin/xuanling.js chat \
  --config xuan_apps/xuanling/config.json \
  --workspace /tmp/xuanling-workspace
```

![xuan + XuanLing](assets/xuanpaw.png)

**Highlights:**

- XuanLing is a minimal conversational agent framework with tool calling
- Runs on resource-constrained devices (including MIPS boards)
- Works with locally deployed LLMs (e.g. Ollama)
- Suited to edge intelligence scenarios

---

## Architecture

```
┌─────────────────────────────────────┐
│           xuan Runtime              │
├─────────────────────────────────────┤
│  Node.js Compatible API Layer       │
├─────────────────────────────────────┤
│  IoT Native Modules (GPIO/I2C/SPI to be added)  │
├─────────────────────────────────────┤
│  QuickJS Engine                     │
├─────────────────────────────────────┤
│  Embedded System (Linux/RTOS)       │
└─────────────────────────────────────┘
```

### Technology stack

- **Core engine**: [QuickJS](https://bellard.org/quickjs/) — fast and lightweight JavaScript engine
- **Runtime base**: built on [tjs (txiki.js)](https://github.com/saghul/txiki.js/), a QuickJS runtime originally aimed at web use cases
- **Design inspiration**: borrows ideas from the AWS Lambda Runtime Thin (LRT)
- **Our positioning**: tjs targets the web, LRT targets serverless — **xuan is built for IoT and embedded systems**
- **Hardware abstraction**: a unified interface for IoT devices
- **Module system**: CommonJS and ES Modules

---

## Built-in IoT modules

xuan ships the essential IoT modules out of the box — no extra installs needed:

| Module | Description |
|--------|-------------|
| **mqtt** | MQTT client for publish/subscribe messaging |
| **serialport** | Serial port communication |
| **ffi** | Call C library functions and reuse existing C libraries |
| **gpio** | GPIO pin control (coming soon) |
| **i2c** | I2C bus communication (coming soon) |
| **spi** | SPI bus communication (coming soon) |

---

## Module system and async

xuan supports **ES Modules** and **CommonJS**, plus async/await throughout:

- `import` / `export`, dynamic `import()`, `import.meta`
- **top-level `await`**
- JSON imports with import attributes
- CommonJS interop: `require()` / `module.exports`

Module resolution: `.mjs` is always ESM, `.cjs` is always CommonJS, and `.js`
follows the nearest `package.json` `"type"` (falling back to content sniffing).

```javascript
// app.mjs
import { readFile } from 'node:fs/promises';

async function loadConfig() {
    const raw = await readFile('./config.json', 'utf8');
    return JSON.parse(raw);
}

// top-level await
const config = await loadConfig();

const os = await import('node:os');       // dynamic import
console.log(import.meta.dirname);         // script directory
console.log(config.id, typeof os.totalmem);
```

```javascript
// JSON import
import data from './data.json' with { type: 'json' };
console.log(data.widget.debug);

// CommonJS interop from ESM
const os = require('node:os');
```

> 💡 The built-in modules use the `tjs:` scheme (e.g. `import FFI from 'tjs:ffi'`),
> a naming convention kept from the txiki.js lineage.

---

## API examples

### MQTT (built-in)

```javascript
const mqtt = require('mqtt');
const client = mqtt.connect('mqtt://broker.emqx.io');

client.on('message', (topic, message) => {
    console.log('recv:', topic, message.toString());
});

client.on('error', (err) => {
    console.error('error:', err);
});

client.on('connect', () => {
    console.log('connect');
    client.subscribe('iot/device/001/#', { qos: 1 }, (err) => {
        if (err) {
            console.log('subscribe error', err);
        } else {
            console.log('subscribe success');
        }
    });
    setInterval(() => {
        client.publish('iot/device/001/status', 'online', { qos: 1 });
    }, 5000);
});
```

### Serial port (built-in)

```javascript
const SerialPort = require('serialport');
const port = new SerialPort('/dev/ttyUSB0', { baudRate: 9600 });

port.write('AT\n');
port.on('data', (data) => {
    console.log('Received:', data.toString());
});
```

### FFI (calling C libraries)

The FFI module lets you call C library functions directly:

```javascript
import FFI from 'tjs:ffi';

// Load a C library
const libc = new FFI.Lib(FFI.Lib.LIBC_NAME);

// Call a C function: atoi
const atoi = new FFI.CFunction(
    libc.symbol('atoi'),
    FFI.types.sint,
    [FFI.types.string]
);
console.log(atoi.call('123'));  // 123

// Call a C function: getenv
const getenv = new FFI.CFunction(
    libc.symbol('getenv'),
    FFI.types.string,
    [FFI.types.string]
);
console.log(getenv.call('HOME'));  // /home/xxx

// Call a C function: time
const time = new FFI.CFunction(
    libc.symbol('time'),
    FFI.types.sint64,
    [FFI.types.pointer]
);
console.log('Current timestamp:', time.call(BigInt(0)));

// Call a custom C function
const mylib = new FFI.Lib('./libmylib.so');
const myfunc = new FFI.CFunction(
    mylib.symbol('my_function'),
    FFI.types.sint,
    [FFI.types.sint, FFI.types.sint]
);
console.log(myfunc.call(1, 2));
```

### GPIO control (coming soon)

```javascript
// const gpio = require('gpio');

// // Output mode
// const led = gpio.open(18, { mode: 'out' });
// led.write(1);  // LED on
// led.write(0);  // LED off

// // Input mode
// const button = gpio.open(23, { mode: 'in', pull: 'up' });
// const state = button.read();
```

### I2C communication (coming soon)

```javascript
// const i2c = require('i2c');
// const device = i2c.open(1, 0x68);  // bus 1, address 0x68

// device.write([0x00, 0x10]);
// const data = device.read(4);
```

---

## Performance

### Cold start benchmark (MacBook Air M1)

| Runtime | Version | Mean cold start | Median | P95 | Executable size |
|---------|---------|-----------------|--------|-----|-----------------|
| **xuan** | dev | 5.37 ms | 5.35 ms | 6.07 ms | 6.2 MB |
| bun | 1.3.11 | 8.82 ms | 8.68 ms | 9.70 ms | 58 MB |
| Node.js | v24.13.0 | 20.29 ms | 20.18 ms | 21.31 ms | 112 MB |

*Test date: 2026-03-27*
*Environment: macOS 13.7.3, Apple M1, Darwin 22.6.0*
*Method: all runtimes invoked with `-e "0"`, 220 runs each, randomly interleaved; wall-clock time from process start to exit.*

> Note: this round only measures cold start time and executable size; resident
> memory was not measured.

### Startup tuning environment variables

Use the `XUAN_*` prefix:

- `XUAN_LIGHTWEIGHT_POLYFILLS=0|1`
  Controls whether the lightweight polyfills startup path is used. By default the
  lightweight path is used for `-e/--eval/eval/-h/--help/-v/--version`. The
  non-lightweight path additionally loads `compat` on top of `lite`.
- `XUAN_HEAVY_WEB_POLYFILLS=0|1`
  Controls whether heavy Web polyfills (such as `CompressionStream` /
  `WebSocketStream`) are loaded, after `lite + compat`.
- `XUAN_STARTUP_PROFILE=1`
  Prints per-phase startup timings (stderr, sorted by duration).

> 💡 **Tip**: the binary can be made even smaller by excluding optional
> components (such as SQLite), which is especially valuable for
> resource-constrained embedded devices.

---

## Building from source

### Requirements

- **macOS**: Xcode or Command Line Tools
- **Linux**: clang-15 or newer
- **MIPS cross-compilation**: requires zig

### Build steps

```bash
git clone https://github.com/topelinux/xuan.git
cd xuan

# Initialize submodules
git submodule update --init --recursive --force

# Linux: use clang-15
export CC=clang-15
export CXX=clang++-15

# Install JS build dependencies (esbuild, polyfills, etc.)
npm install

# Build
make

# Run the test suite
make test
```

### JS bundle build pipeline

xuan compiles JavaScript modules into C files (`src/bundles/c/**/*.c`) that are
embedded into the binary.

These C bundles are build artifacts and are not committed (the repository
ignores `src/bundles/c/`).

**Local build:**
```bash
make
# generates/updates bundles automatically, then compiles
```

**After modifying JS sources:**
```bash
make js
make
```

**Cross build:**
```bash
# The script runs `make js` on the host automatically before cross compiling
BUILD_WITH_MIMALLOC=ON TJS_WAMR_SIMD=ON ./scripts/build-aarch64.sh
```

### Cross compilation

```bash
# ARM64
make TARGET=arm64

# RISC-V
make TARGET=riscv64

# AArch64 (GNU/Linux, dynamically linked, interpreter usually /lib/ld-linux-aarch64.so.1)
# Install zig first: brew install zig (macOS) or apt install zig (Linux)
# Without a sysroot/prebuilt libffi, a dynamic libffi (libffi.so*) is built from deps_lib/libffi-3.5.2
# WAMR SIMD is off by default to avoid fetching simde during cross builds (enable with TJS_WAMR_SIMD=ON)
# Minimal (no sysroot):
./scripts/build-aarch64.sh
# With a sysroot:
TJS_AARCH64_SYSROOT=/path/to/aarch64-sysroot ./scripts/build-aarch64.sh

# MIPS (dynamic linking)
# Install zig first: brew install zig (macOS) or apt install zig (Linux)
TJS_MIPS_SYSROOT=/path/to/target-mipsel_24kc_musl TJS_LINK_MODE=dynamic TJS_USE_DEPS_LIBUV_SHARED=ON ./scripts/build-mips-static.sh
```

---

## Acknowledgements

xuan is built on these excellent projects:

- **[tjs (txiki.js)](https://github.com/saghul/txiki.js/)** — a QuickJS runtime by Saúl Ibarra Corretgé with excellent Node.js compatibility and a lightweight design. This project is forked from it and continues under the MIT license.
- **[QuickJS](https://bellard.org/quickjs/)** — a lightweight JavaScript engine by Fabrice Bellard.

---

## Contributing

Contributions are welcome! Feel free to open issues, fork the repository and
send pull requests.

1. Fork the project
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## License

MIT License — see [LICENSE](LICENSE).

---

<div align="center">

**xuan** — JavaScript for Every Device

Made with ⚡ by the xuan Team

</div>
