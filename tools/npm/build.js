import * as esbuild from 'esbuild';
import { existsSync, mkdirSync } from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const binDir = path.join(__dirname, 'bin');
const distDir = path.join(__dirname, 'dist');

if (!existsSync(distDir)) {
    mkdirSync(distDir, { recursive: true });
}

await esbuild.build({
    entryPoints: [path.join(binDir, 'tjs-npm.js')],
    outfile: path.join(distDir, 'tjs-npm.js'),
    bundle: true,
    minify: true,
    platform: 'neutral',
    target: 'esnext',
    format: 'esm',
    external: ['tjs:*']
});

console.log('Built dist/tjs-npm.js (minified)');
