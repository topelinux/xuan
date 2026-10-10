const modA = require('./circular-a');

exports.name = 'circular-b';
exports.value = 42;
exports.getModA = () => modA;
