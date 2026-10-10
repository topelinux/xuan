const mod = require('./check-filename-dirname');
console.log(JSON.stringify({
    hasFilename: typeof mod.filename === 'string',
    hasDirname: typeof mod.dirname === 'string',
    filenameEndsWith: mod.filename,
    dirnameEndsWith: mod.dirname
}));
