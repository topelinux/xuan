import assert from 'tjs:assert';
import http from 'http';
import https from 'https';

const decoder = new TextDecoder();
const originalFetch = globalThis.fetch;
const calls = [];

globalThis.fetch = async (url, init = {}) => {
  calls.push({ url: String(url), init });

  let bodyText = '';
  if (init.body instanceof Uint8Array) {
    bodyText = decoder.decode(init.body);
  }

  return new Response(`echo:${bodyText || 'empty'}`, {
    status: 201,
    statusText: 'Created',
    headers: {
      'x-test': 'ok'
    }
  });
};

try {
  const httpResult = await new Promise((resolve, reject) => {
    const req = http.request('http://example.com/phase1', {
      method: 'POST',
      headers: {
        'x-req': '1'
      }
    }, (res) => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (chunk) => {
        body += chunk;
      });
      res.on('end', () => {
        resolve({
          statusCode: res.statusCode,
          statusMessage: res.statusMessage,
          header: res.headers['x-test'],
          body
        });
      });
      res.on('error', reject);
    });

    req.on('error', reject);
    req.write('hello');
    req.write(' world');
    req.end();
  });

  console.log('Response', httpResult.body);
  assert.eq(httpResult.statusCode, 201);
  assert.eq(httpResult.statusMessage, 'Created');
  assert.eq(httpResult.header, 'ok');
  assert.eq(httpResult.body, 'echo:hello world');

  await new Promise((resolve, reject) => {
    const req = https.get('https://example.com/secure?p=1', (res) => {
      res.on('data', () => { });
      res.on('end', resolve);
      res.on('error', reject);
    });
    req.on('error', reject);
  });

  assert.eq(calls.length, 2);
  assert.eq(calls[0].url, 'http://example.com/phase1');
  assert.eq(calls[0].init.method, 'POST');
  assert.eq(calls[1].url, 'https://example.com/secure?p=1');
  assert.eq(calls[1].init.method, 'GET');
} finally {
  globalThis.fetch = originalFetch;
}
