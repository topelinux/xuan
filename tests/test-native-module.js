import assert from 'tjs:assert';

let modPath = '../build/native-test.so';
switch (navigator.userAgentData.platform) {
    case 'linux':
        modPath = '../build/native-test.so';
        break;
    case 'darwin':
        modPath = '../build/native-test.so';
        break;
    case 'windows':
        modPath = '../build/native-test.dll';
        break;
}

const mod = await import(modPath);
assert.eq(typeof mod.answer, 'function');
assert.eq(mod.answer(), 42);
assert.eq(mod.version, 1);
