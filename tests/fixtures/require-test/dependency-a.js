const depB = require('./dependency-b');

exports.name = 'dependency-a';
exports.depBValue = depB.value;
exports.getValue = () => depB.value + 10;
