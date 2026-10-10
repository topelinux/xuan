exports.answer = 42;
module.exports.mode = 'cjs';

console.log(JSON.stringify({
    exportsType: typeof exports,
    moduleType: typeof module,
    answer: exports.answer,
    mode: module.exports.mode
}));
