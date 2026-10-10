import assert from 'tjs:assert';
import path from 'tjs:path';
import { runAndParse } from './helpers/test-utils.js';

const entry = path.join(import.meta.dirname, 'fixtures', 'native-modules', 'entry.mjs');
const packageDir = path.join(import.meta.dirname, 'fixtures', 'native-module-packages');
const nodePath = tjs.env.NODE_PATH ? `${packageDir}${path.delimiter}${tjs.env.NODE_PATH}` : packageDir;
const result = await runAndParse(entry, {
    env: {
        ...tjs.env,
        NODE_PATH: nodePath,
    }
});

assert.eq(result.answer, 42);
assert.eq(result.version, 1);
