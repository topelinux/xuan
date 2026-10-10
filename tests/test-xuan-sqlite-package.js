import assert from 'tjs:assert';
import path from 'tjs:path';
import { runAndParse } from './helpers/test-utils.js';

const entry = path.join(import.meta.dirname, 'fixtures', 'native-modules', 'entry-xuan-sqlite.mjs');
const result = await runAndParse(entry);

assert.eq(result.rows, 1);
assert.eq(result.n, 7);
