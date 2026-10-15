// Cold-start timing: spawn the runtime under test N times and report the
// per-run wall time (ms) as key=value lines.
//
// Env:
//   XUAN_BIN    path to the runtime executable (required)
//   BENCH_ARGS  arguments passed to every run, space separated (default "-e 0")
//   RUNS        measured runs (default 200)
//   WARMUP      discarded warmup runs (default 10)
const cp = require('child_process');

const bin = process.env.XUAN_BIN;
if (!bin) {
    console.error('XUAN_BIN is required');
    process.exit(2);
}

const args = (process.env.BENCH_ARGS || '-e 0').split(' ').filter(Boolean);
const runs = Number(process.env.RUNS || 200);
const warmup = Number(process.env.WARMUP || 10);

const now = typeof performance !== 'undefined' && performance.now
    ? () => performance.now()
    : () => Date.now();

function once() {
    const t0 = now();
    const r = cp.spawnSync(bin, args, { stdio: 'ignore' });
    const t1 = now();

    if (r.status !== 0) {
        throw new Error('spawn failed, status=' + r.status);
    }

    return t1 - t0;
}

for (let i = 0; i < warmup; i++) {
    once();
}

const times = [];

for (let i = 0; i < runs; i++) {
    times.push(once());
}

times.sort((a, b) => a - b);

const q = (p) => times[Math.min(times.length - 1, Math.floor(p * times.length))];
const mean = times.reduce((a, b) => a + b, 0) / times.length;

console.log('n=' + times.length);
console.log('min=' + times[0].toFixed(3));
console.log('median=' + q(0.5).toFixed(3));
console.log('p95=' + q(0.95).toFixed(3));
console.log('mean=' + mean.toFixed(3));
