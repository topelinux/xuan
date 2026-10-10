import assert from 'tjs:assert';
import url from 'url';
import nodeUrl from 'node:url';

const requiredUrlNs = require('url');
const requiredUrl = requiredUrlNs.default ?? requiredUrlNs;

assert.eq(typeof url.parse, 'function');
assert.eq(typeof nodeUrl.parse, 'function');
assert.eq(typeof requiredUrl.parse, 'function');

{
    const parsed = url.parse('/modem/state');
    assert.eq(parsed.pathname, '/modem/state');
    assert.eq(parsed.protocol, 'http:');
    assert.eq(parsed.host, 'localhost');
    assert.eq(parsed.hostname, 'localhost');
    assert.eq(parsed.href, 'http://localhost/modem/state');
}

{
    const parsed = nodeUrl.parse('/modem/state');
    assert.eq(parsed.pathname, '/modem/state');
    assert.eq(parsed.protocol, 'http:');
    assert.eq(parsed.host, 'localhost');
}

{
    const parsed = requiredUrl.parse('/modem/state');
    assert.eq(parsed.pathname, '/modem/state');
    assert.eq(parsed.protocol, 'http:');
    assert.eq(parsed.host, 'localhost');
}

{
    const parsed = url.parse('http://example.com/path');
    assert.eq(parsed.pathname, '/path');
    assert.eq(parsed.protocol, 'http:');
    assert.eq(parsed.host, 'example.com');
    assert.eq(parsed.hostname, 'example.com');
    assert.eq(parsed.href, 'http://example.com/path');
}

{
    const parsed = url.parse('https://user:pass@example.com:8080/foo?bar=baz#hash');
    assert.eq(parsed.protocol, 'https:');
    assert.eq(parsed.auth, 'user:pass');
    assert.eq(parsed.host, 'example.com:8080');
    assert.eq(parsed.hostname, 'example.com');
    assert.eq(parsed.pathname, '/foo');
    assert.eq(parsed.search, '?bar=baz');
    assert.eq(parsed.hash, '#hash');
    assert.eq(parsed.query, 'bar=baz');
}

{
    const parsed = url.parse('//example.com/path');
    assert.eq(parsed.pathname, '/path');
    assert.eq(parsed.host, 'example.com');
    assert.eq(parsed.protocol, 'http:');
}

{
    const parsed = url.parse('/foo/bar?a=1', true);
    assert.eq(parsed.pathname, '/foo/bar');
    assert.eq(parsed.query.a, '1');
}

console.log('URL parse tests passed!');
