import assert from 'tjs:assert';
import path from 'tjs:path';
import { runAndParse } from './helpers/test-utils.js';

const testDir = path.join(import.meta.dirname, 'fixtures', 'require-test');

// Test 1: Basic require functionality
async function testRequireBasic() {
    const result = await runAndParse(path.join(testDir, 'basic-require.js'));
    assert.eq(result.simpleModule.value, 42, 'require simple module exports value');
    assert.eq(result.simpleModule.name, 'simple-module', 'require simple module exports name');
    assert.eq(result.simpleModule.extra, 'extra-data', 'require module.exports assignment');
    assert.eq(result.cached, true, 'require returns same cached module');
}

// Test 2: JSON file require
async function testRequireJson() {
    const result = await runAndParse(path.join(testDir, 'require-json.js'));
    assert.eq(result.name, 'test-json-file', 'require JSON file name field');
    assert.eq(result.version, '1.0.0', 'require JSON file version field');
    assert.eq(result.value, 123, 'require JSON file value field');
}

// Test 3: Module dependencies
async function testRequireDependencies() {
    const result = await runAndParse(path.join(testDir, 'require-dependencies.js'));
    assert.eq(result.depA.name, 'dependency-a', 'module with dependencies name');
    assert.eq(result.depA.depBValue, 100, 'dependency value accessible');
    assert.eq(result.depAGetValueResult, 110, 'dependency function call works');
}

// Test 4: Nested directory require
async function testRequireNested() {
    const result = await runAndParse(path.join(testDir, 'require-nested.js'));
    assert.eq(result.fromNested, 'nested-index', 'nested index.js require');
    assert.eq(result.parent.name, 'parent-module', 'nested ../ parent require');
    assert.eq(result.parent.value, 999, 'nested parent value');
}

// Test 5: node_modules resolution
async function testRequireFromNodeModules() {
    const result = await runAndParse(path.join(testDir, 'require-from-node-modules.js'));
    assert.eq(result.pkg.name, 'pkg-with-package', 'require package with package.json');
    assert.eq(result.pkg.version, '1.0.0', 'require package version');
    assert.eq(result.standalone.standalone, true, 'require single-file module from node_modules');
}

// Test 6: Require cache behavior
async function testRequireCache() {
    const result = await runAndParse(path.join(testDir, 'require-cache.js'));
    assert.eq(result.cached, true, 'require returns cached instance');
    assert.eq(result.mutation, 'modified', 'cache mutation visible across requires');
}

// Test 7: Circular dependencies
async function testRequireCircular() {
    const result = await runAndParse(path.join(testDir, 'require-circular.js'));
    assert.eq(result.circularA.name, 'circular-a', 'circular dependency module A loaded');
    assert.eq(result.circularB.name, 'circular-b', 'circular dependency module B loaded');
    assert.eq(result.circularB.value, 42, 'circular dependency value accessible');
}

// Test 8: __filename and __dirname
async function testRequireFilenameDirname() {
    const result = await runAndParse(path.join(testDir, 'require-filename-dirname.js'));
    assert.eq(result.hasFilename, true, '__filename is defined');
    assert.eq(result.hasDirname, true, '__dirname is defined');
    assert.ok(result.filenameEndsWith.includes('check-filename-dirname.js'), '__filename contains filename');
    assert.ok(result.dirnameEndsWith.includes('require-test'), '__dirname contains dirname');
}

// Run all tests
try {
    console.log('Running require tests...\n');
    
    await testRequireBasic();
    console.log('✓ testRequireBasic');

    await testRequireJson();
    console.log('✓ testRequireJson');

    await testRequireDependencies();
    console.log('✓ testRequireDependencies');

    await testRequireNested();
    console.log('✓ testRequireNested');

    await testRequireFromNodeModules();
    console.log('✓ testRequireFromNodeModules');

    await testRequireCache();
    console.log('✓ testRequireCache');

    await testRequireCircular();
    console.log('✓ testRequireCircular');

    await testRequireFilenameDirname();
    console.log('✓ testRequireFilenameDirname');

    console.log('\n✓ All require tests passed!');
} catch (e) {
    console.error('\n✗ Test failed:', e.message);
    console.error(e.stack);
    tjs.exit(1);
}
