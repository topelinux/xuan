const mod1 = require('./simple-module');
const mod2 = require('./simple-module');

mod1.testMutation = 'modified';

console.log(JSON.stringify({
    cached: mod1 === mod2,
    mutation: mod2.testMutation
}));
