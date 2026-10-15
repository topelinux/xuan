# Benchmarks

Per-target numbers for xuan: binary size, cold start, and resident memory
(RSS) for an idle script and for a minimal MQTT subscriber.

The point is a **reproducible, scripted** methodology rather than a one-off
measurement, so the numbers can be regenerated on every target (including
embedded boards) and tracked over time.

## Methodology

Harness: [`bench/`](../bench)

| File | Purpose |
| --- | --- |
| `bench/run.sh` | single entry point; measures one target per invocation |
| `bench/coldstart.js` | times N process spawns using the runtime under test |
| `bench/hello.js` | minimal script (cold start / baseline) |
| `bench/idle.js` | long-lived minimal script (baseline RSS) |
| `bench/mqtt-sub.js` | minimal MQTT subscriber (application RSS) |

Definitions:

- **Binary size** — on-disk size of the runtime executable (`wc -c`). Not
  stripped, not compressed; it does not include the shared libs that the
  package ships next to it.
- **Cold start** — wall-clock time from `spawn` to process exit, timed with
  `child_process.spawnSync` *inside the runtime under test*, 10 warmup + 200
  measured runs; reported as min / median / p95. Two payloads: `-e 0` (engine
  startup) and a script file (`bench/hello.js`, which adds module resolution).
- **RSS** — process resident set, sampled once per second for 30 s
  (`VmRSS` from `/proc/<pid>/status` on Linux, `ps -o rss=` elsewhere);
  reported as **peak** (high-water mark of the window) and **floor** (settled
  value).
- **MQTT RSS** — RSS of `bench/mqtt-sub.js` after it reports `READY` (that is,
  after connecting and subscribing to a local broker).

### Running it

```sh
# macOS / Linux
mosquitto -p 1883 &                                    # broker for the MQTT figure
bench/run.sh --xuan ./build/xuan --label macos-arm64

# embedded board, binary shipped next to its libs
LD_LIBRARY_PATH=/ruff/xuan/lib bench/run.sh \
    --xuan /ruff/xuan/bin/xuan --label mips-7621
```

Results are printed as `key=value` lines and appended to
`bench/results/<label>.tsv`. Pass `--no-mqtt` to skip the broker-dependent
figure.

## Results

### Binary size and cold start

| Target | Build | Binary | `-e 0` min/med/p95 (ms) | script min/med/p95 (ms) |
| --- | --- | --- | --- | --- |
| macos-arm64 | dev `a7217c2` | 4.3 MiB | 6.02 / 6.47 / 7.36 | 10.38 / 10.80 / 12.18 |
| linux-mips (MT7621) | dev 2026-10-13 | 4.1 MiB | 73.4 / 74.1 / 79.6 | 223.5 / 224.7 / 230.9[^mips-polyfills] |
| linux-x86_64 | — | _pending_ | _pending_ | _pending_ |
| windows-x86_64 | — | _pending_ | _pending_ | _pending_ |
| linux-riscv64 | — | _pending_ | _pending_ | _pending_ |

*macos-arm64 environment: macOS 13.7.3, Apple M1, Darwin 22.6.0; xuan dev build
(quickjs 0.15.1, libuv 1.51.1-dev, lws 4.5.2, mimalloc 3.2.7).*

*linux-mips environment: MediaTek MT7621 (MIPS 1004Kc), Linux 5.10.221, 249 MB
RAM; xuan dev build (quickjs 0.15.1, libuv 1.51.0, lws 4.5.2, wamr 2.4.4; no
mimalloc).*

[^mips-polyfills]: Running a **script file** eagerly evaluates the heavy web
polyfills: `XUAN_STARTUP_PROFILE=1` attributes ~134 ms of the 224 ms to
`eval_polyfills_heavy_bytecode`. With `XUAN_LIGHTWEIGHT_POLYFILLS=1` the same
script starts in ~77 ms — the same as `-e 0`, which already takes the
lightweight path.

### Resident memory (RSS)

| Target | idle peak / floor | MQTT subscriber peak / floor |
| --- | --- | --- |
| macos-arm64 | 5.3 / 2.4 MiB | 10.0 / 5.3 MiB[^macos-rss] |
| linux-mips (MT7621) | 4.6 / 1.0 MiB | 6.6 / 6.6 MiB |
| linux-x86_64 | _pending_ | _pending_ |
| windows-x86_64 | _pending_ | _pending_ |
| linux-riscv64 | _pending_ | _pending_ |

[^macos-rss]: macOS memory accounting is not stable enough to compare across
runtimes: the kernel reclaims clean code pages over time (RSS can settle below
the size of the binary itself), and the MQTT path additionally shows an
OS-tagged `IOAccelerator` mapping (up to ~18 MB) that RSS may or may not count.
Treat the macOS RSS row as indicative; the Linux/MIPS rows are the ones to use
for sizing.

## Caveats

- **Allocator.** xuan builds with [mimalloc]; RSS depends on its commit/purge
  behaviour, and a build without it will differ.
- **Build configuration.** The numbers above come from a dev build. Release
  artifacts may be stripped or built with different flags — compare like with
  like and record the build id.
- **libc.** glibc vs musl (OpenWrt) changes both binary size and RSS.
- **libwebsockets.** The MQTT client links libwebsockets, which dominates the
  MQTT figure. A plain TCP connection costs much less (`bench/run.sh` does not
  measure it; see `bench/idle.js` as the floor).
- **Timing conditions.** Cold start is sensitive to CPU frequency, storage and
  page cache. Run the harness on an idle machine.

[mimalloc]: https://github.com/microsoft/mimalloc
