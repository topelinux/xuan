import path from 'tjs:path';


console.log(JSON.stringify({
    filenameType: typeof __filename,
    dirnameType: typeof __dirname,
    dirnameMatches: path.dirname(__filename) === __dirname,
    basename: path.basename(__filename)
}));
