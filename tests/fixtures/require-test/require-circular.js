const circularA = require('./circular-a');
const circularB = require('./circular-b');

console.log(JSON.stringify({
    circularA: { name: circularA.name },
    circularB: { name: circularB.name, value: circularB.value }
}));
