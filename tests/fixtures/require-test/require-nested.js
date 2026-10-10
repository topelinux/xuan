const nested = require('./nested');
const parent = nested.parent;
console.log(JSON.stringify({ fromNested: nested.fromNested, parent }));
