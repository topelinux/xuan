import assert from 'tjs:assert';

function testParseAndStringifyRoundTrip() {
    const source = '{"x":1,"y":true,"z":null,"a":[1,2,3],"s":"str"}';
    const value = JSON.parse(source);

    assert.eq(value.x, 1, 'parse integer field');
    assert.eq(value.y, true, 'parse boolean field');
    assert.eq(value.z, null, 'parse null field');
    assert.eq(value.a.length, 3, 'parse array field');
    assert.eq(value.s, 'str', 'parse string field');
    assert.eq(JSON.stringify(value), source, 'stringify round trip');
}

function testReviverTraversal() {
    const calls = [];
    const value = JSON.parse('{"a":1,"b":{"c":2},"d":[3,4]}', (key, val) => {
        calls.push(key);
        if (key === 'c') {
            return val + 40;
        }
        if (key === '0') {
            return undefined;
        }
        return val;
    });

    assert.eq(value.a, 1, 'reviver keeps scalar');
    assert.eq(value.b.c, 42, 'reviver updates nested object field');
    assert.eq(0 in value.d, false, 'reviver can delete array element');
    assert.eq(value.d[1], 4, 'reviver keeps other array elements');
    assert.ok(calls.includes(''), 'reviver visits root');
}

function testStringifyReplacerFunction() {
    const input = {
        keep: 1,
        drop: 2,
        nested: { value: 3 }
    };
    const out = JSON.stringify(input, (key, value) => {
        if (key === 'drop') {
            return undefined;
        }
        if (key === 'value') {
            return value * 2;
        }
        return value;
    });

    assert.eq(out, '{"keep":1,"nested":{"value":6}}', 'replacer function transforms output');
}

function testStringifyReplacerArray() {
    const input = {
        keep: 1,
        drop: 2,
        nested: 3,
        7: 'seven'
    };
    const out = JSON.stringify(input, ['nested', 'keep', 7, '7', 'keep']);

    assert.eq(out, '{"nested":3,"keep":1,"7":"seven"}', 'replacer array filters and deduplicates keys');
}

function testIndentation() {
    const out = JSON.stringify([[{ x: 1, y: {}, z: [] }, 2, 3]], undefined, 1);

    assert.eq(out, `[
 [
  {
   "x": 1,
   "y": {},
   "z": []
  },
  2,
  3
 ]
]`, 'stringify indentation matches expected shape');
}

function testEdgeCases() {
    assert.eq(JSON.stringify({ nan: NaN, inf: Infinity, ninf: -Infinity }),
              '{"nan":null,"inf":null,"ninf":null}',
              'non-finite numbers stringify to null');
    assert.eq(JSON.stringify([undefined, function () {}, Symbol('x')]),
              '[null,null,null]',
              'unsupported array values stringify to null');
    assert.eq(JSON.stringify({ undef: undefined, fn: function () {}, sym: Symbol('x') }),
              '{}',
              'unsupported object values are omitted');

    let threw = false;
    try {
        JSON.stringify({ x: 1n });
    } catch (e) {
        threw = e instanceof TypeError;
    }
    assert.eq(threw, true, 'BigInt stringify throws TypeError');
}

function testCircularReferenceThrows() {
    const value = { name: 'root' };
    value.self = value;

    let threw = false;
    try {
        JSON.stringify(value);
    } catch (e) {
        threw = e instanceof TypeError;
    }
    assert.eq(threw, true, 'circular reference throws TypeError');
}

testParseAndStringifyRoundTrip();
testReviverTraversal();
testStringifyReplacerFunction();
testStringifyReplacerArray();
testIndentation();
testEdgeCases();
testCircularReferenceThrows();
