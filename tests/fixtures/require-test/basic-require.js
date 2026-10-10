const mod = require('./simple-module');
const mod2 = require('./simple-module');

const result = {
    simpleModule: mod,
    cached: mod === mod2
};

console.log(JSON.stringify(result));
