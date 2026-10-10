const modB = require('./circular-b');

exports.name = 'circular-a';
exports.getValue = () => modB.value || 'not-yet-loaded';
exports.getModB = () => modB;
