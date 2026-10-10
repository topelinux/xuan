#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as esbuild from 'esbuild';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..', '..');

const DEFAULT_ENTRY = path.resolve(ROOT, 'lvgl-demo/lv_binding_js/demo/calculator/index.jsx');
const DEFAULT_OUT = path.resolve(ROOT, 'build/lvgl-demo/calculator.js');
const DEFAULT_ALIAS = path.resolve(ROOT, 'scripts/lvgl/lvgljs-ui.ts');
const LVGLJS_BRIDGE = path.resolve(ROOT, 'xuan_modules/lvgljs/index.js');
const LVGL_DEMO_ROOT = path.resolve(ROOT, 'lvgl-demo/lv_binding_js');

function parseArgs(argv) {
    const args = {
        entry: DEFAULT_ENTRY,
        out: DEFAULT_OUT,
        alias: DEFAULT_ALIAS,
        assetBase: null,
        depsRoot: LVGL_DEMO_ROOT,
        metafile: null,
        minify: false,
        sourcemap: false,
        mode: 'development',
    };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--entry' && argv[i + 1]) {
            args.entry = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--out' && argv[i + 1]) {
            args.out = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--alias' && argv[i + 1]) {
            args.alias = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--asset-base' && argv[i + 1]) {
            args.assetBase = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--deps-root' && argv[i + 1]) {
            args.depsRoot = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--metafile' && argv[i + 1]) {
            args.metafile = path.resolve(ROOT, argv[++i]);
        } else if (arg === '--production') {
            args.mode = 'production';
            args.minify = true;
        } else if (arg === '--minify') {
            args.minify = true;
        } else if (arg === '--sourcemap') {
            args.sourcemap = true;
        } else if (arg === '--mode' && argv[i + 1]) {
            args.mode = argv[++i];
        } else if (arg === '-h' || arg === '--help') {
            printHelp();
            process.exit(0);
        }
    }

    return args;
}

function printHelp() {
    console.log('Usage: node scripts/lvgl/build-demo.mjs [options]');
    console.log('');
    console.log('Options:');
    console.log('  --entry <path>    Entry file (default: lvgl-demo/lv_binding_js/demo/calculator/index.jsx)');
    console.log('  --out <path>      Output file (default: build/lvgl-demo/calculator.js)');
    console.log('  --alias <path>    lvgljs-ui alias target (default: scripts/lvgl/lvgljs-ui.ts)');
    console.log('  --asset-base <p>  Base directory for runtime asset resolution');
    console.log('  --deps-root <p>   Dependency root for React/lvgljs-ui deps (default: lvgl-demo/lv_binding_js)');
    console.log('  --mode <mode>     development | production (default: development)');
    console.log('  --production      Shortcut for --mode production --minify');
    console.log('  --minify          Enable minify');
    console.log('  --sourcemap       Generate sourcemap');
    console.log('  --metafile <path> Write esbuild metafile JSON');
    console.log('  -h, --help        Show this help');
}

function lvglUiAliasPlugin(aliasTarget) {
    return {
        name: 'lvgljs-ui-alias',
        setup(build) {
            build.onResolve({ filter: /^lvgljs-ui$/ }, () => ({ path: aliasTarget }));
        },
    };
}

function toImportSpecifier(fromFile, targetFile) {
    let rel = path.relative(path.dirname(fromFile), targetFile).split(path.sep).join('/');
    if (!rel.startsWith('.')) {
        rel = `./${rel}`;
    }
    return rel;
}

async function main() {
    const args = parseArgs(process.argv.slice(2));

    if (!fs.existsSync(args.entry)) {
        throw new Error(`Entry not found: ${args.entry}`);
    }
    if (!fs.existsSync(args.alias)) {
        throw new Error(`Alias target not found: ${args.alias}`);
    }
    if (!fs.existsSync(LVGLJS_BRIDGE)) {
        throw new Error(`LVGL bridge not found: ${LVGLJS_BRIDGE}`);
    }

    fs.mkdirSync(path.dirname(args.out), { recursive: true });
    const bridgeImport = toImportSpecifier(args.out, LVGLJS_BRIDGE);
    const assetBase = (args.assetBase || LVGL_DEMO_ROOT).split(path.sep).join('/');
    const useMetafile = !!args.metafile;

    const result = await esbuild.build({
        absWorkingDir: ROOT,
        entryPoints: [args.entry],
        outfile: args.out,
        bundle: true,
        format: 'esm',
        platform: 'neutral',
        target: ['es2020'],
        nodePaths: [path.join(args.depsRoot, 'node_modules')],
        logLevel: 'info',
        minify: args.minify,
        sourcemap: args.sourcemap,
        metafile: useMetafile,
        define: {
            'process.env.NODE_ENV': JSON.stringify(args.mode),
        },
        banner: {
            js: `globalThis.__LVGL_ASSET_BASE = ${JSON.stringify(assetBase)};\nimport '${bridgeImport}';`,
        },
        external: [
            'tjs:path',
        ],
        plugins: [
            lvglUiAliasPlugin(args.alias),
        ],
    });

    if (useMetafile) {
        fs.mkdirSync(path.dirname(args.metafile), { recursive: true });
        fs.writeFileSync(args.metafile, JSON.stringify(result.metafile, null, 2));
        console.log(`Wrote metafile: ${path.relative(ROOT, args.metafile)}`);
    }

    console.log(`Built LVGL demo bundle: ${path.relative(ROOT, args.out)}`);
}

main().catch((err) => {
    console.error('[lvgl-build] failed:', err && err.message ? err.message : err);
    process.exit(1);
});
