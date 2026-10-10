const path = require('path');

exports.answer = 42;
module.exports.mode = 'cjs';

console.log(JSON.stringify({
    exportsType: typeof exports,
    moduleType: typeof module,
    filenameType: typeof __filename,
    dirnameType: typeof __dirname,
    basename: path.basename(__filename),
    dirnameMatches: path.dirname(__filename) === __dirname,
    moduleFilename: module.filename,
    modulePath: module.path,
    moduleLoaded: module.loaded,
    moduleRequireType: typeof module.require,
    answer: exports.answer,
    mode: module.exports.mode
}));
