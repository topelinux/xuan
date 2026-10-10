// Compatibility polyfills layered on top of lite polyfills.
// Order is important.

// Note: TypedArray.prototype.copy polyfill is in lite.js (needed for grpc-js).

import './navigator.js';

import './blob.js';
import './file.js';
import './file-reader.js';
import './form-data.js';
import './xhr.js';
import './fetch/polyfill.js';

import './storage.js';
import './wasm.js';
import './worker.js';
import './ws.js';
