#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..', '..');

function parseArgs(argv) {
    const args = {
        bundle: path.resolve(ROOT, 'build/lvgl-demo/widgets.js'),
        xuan: path.resolve(ROOT, 'build/xuan'),
        module: path.resolve(ROOT, 'build/dist/node_modules/lvgl/native/xuan-lvgl.so'),
        run: false,
        timeout: 3000,
        env: {
            SDL_VIDEODRIVER: 'dummy',
        },
    };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--bundle' && argv[i + 1]) {
            args.bundle = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--xuan' && argv[i + 1]) {
            args.xuan = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--module' && argv[i + 1]) {
            args.module = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--timeout' && argv[i + 1]) {
            args.timeout = Math.max(1, Number(argv[++i]) || args.timeout);
        } else if (arg === '--env' && argv[i + 1]) {
            const pair = argv[++i];
            const idx = pair.indexOf('=');
            if (idx > 0) {
                args.env[pair.slice(0, idx)] = pair.slice(idx + 1);
            }
        } else if (arg === '--run') {
            args.run = true;
        } else if (arg === '-h' || arg === '--help') {
            printHelp();
            process.exit(0);
        }
    }
    return args;
}

function printHelp() {
    console.log('Usage: node scripts/lvgl/measure-app.mjs [options]');
    console.log('');
    console.log('Options:');
    console.log('  --bundle <path>   Bundle to measure (default: build/lvgl-demo/widgets.js)');
    console.log('  --xuan <path>     xuan binary (default: build/xuan)');
    console.log('  --module <path>   xuan-lvgl module path');
    console.log('  --run             Run the bundle and report elapsed wall time');
    console.log('  --timeout <ms>    Run timeout in milliseconds (default: 3000)');
    console.log('  --env K=V         Extra environment for --run; repeatable');
    console.log('  -h, --help        Show this help');
}

function fileSize(file) {
    try {
        return fs.statSync(file).size;
    } catch (_) {
        return null;
    }
}

function rel(file) {
    return path.relative(ROOT, file);
}

function main() {
    const args = parseArgs(process.argv.slice(2));
    const report = {
        bundle: rel(args.bundle),
        bundleBytes: fileSize(args.bundle),
        xuan: rel(args.xuan),
        xuanBytes: fileSize(args.xuan),
        lvglModule: rel(args.module),
        lvglModuleBytes: fileSize(args.module),
    };

    if (args.run) {
        const started = Date.now();
        const result = spawnSync(args.xuan, [args.bundle], {
            cwd: ROOT,
            env: {
                ...process.env,
                ...args.env,
            },
            encoding: 'utf8',
            timeout: args.timeout,
        });
        report.run = {
            elapsedMs: Date.now() - started,
            status: result.status,
            signal: result.signal,
            timedOut: result.error?.code === 'ETIMEDOUT',
        };
        if (result.error && result.error.code !== 'ETIMEDOUT') {
            report.run.error = result.error.message;
        }
    }

    console.log(JSON.stringify(report, null, 2));
}

main();
