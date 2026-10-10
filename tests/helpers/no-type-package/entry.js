const dep = require('./dep');

console.log(JSON.stringify({
    exportsType: typeof exports,
    moduleType: typeof module,
    requireType: typeof require,
    value: dep.value
}));
