const pkg = require('pkg-with-package');
const standalone = require('single-file-module.cjs');
console.log(JSON.stringify({ pkg, standalone }));
