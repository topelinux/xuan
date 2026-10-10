import * as native from 'native-test-module-pkg';

console.log(JSON.stringify({
    answer: native.answer(),
    version: native.version,
}));
