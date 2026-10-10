import path from 'tjs:path';

import child from './import-filename-dirname-child.js';


console.log(JSON.stringify({
    mainFilename: __filename,
    mainDirname: __dirname,
    childFilename: child.filename,
    childDirname: child.dirname,
    mainMatches: path.dirname(__filename) === __dirname,
    childMatches: path.dirname(child.filename) === child.dirname,
    sameFile: __filename === child.filename,
    mainBasename: path.basename(__filename),
    childBasename: path.basename(child.filename)
}));
