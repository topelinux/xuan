import assert from 'tjs:assert';
import http from 'tjs:http';
import path from "tjs:path";

const TJS_HOME = tjs.env.TJS_HOME ?? path.join(tjs.homeDir, '.tjs');
const cookieJarPath = path.join(TJS_HOME, 'cookies.txt');

const cookieValue = Math.random().toString();
// Use an explicit Expires so the cookie jar stores it (session cookies are not persisted).
const cookieHeader = `key=${cookieValue}; Expires=Thu, 01 Jan 2099 00:00:00 GMT; Path=/`;

// Start a local HTTP server that can set and echo cookies.
const server = http.createServer((req, res) => {
    if (req.url === '/set-cookie') {
        res.setHeader('Set-Cookie', cookieHeader);
        res.end('cookie set');
    } else if (req.url === '/cookies') {
        // Echo back the cookies as JSON.
        const cookies = req.headers.cookie || '';
        const cookieObj = {};
        cookies.split(';').forEach(cookie => {
            const [name, ...rest] = cookie.trim().split('=');
            if (name) {
                cookieObj[name] = rest.join('=');
            }
        });
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({ cookies: cookieObj }));
    } else {
        res.statusCode = 404;
        res.end('not found');
    }
});

await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
const baseUrl = `http://127.0.0.1:${port}`;

function doXhr(reqUrl, withCredentials) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.withCredentials = withCredentials;
        xhr.open('GET', reqUrl);
        xhr.onload = () => resolve(xhr);
        xhr.onerror = () => reject(new Error('XHR error'));
        xhr.send();
    });
}

// Set cookie via local server.
const xhr1 = await doXhr(`${baseUrl}/set-cookie`, true);
assert.eq(xhr1.readyState, xhr1.DONE, 'readyState is DONE');
assert.eq(xhr1.status, 200, 'status is 200');

// Without credentials: cookie should not be sent.
const xhr2 = await doXhr(`${baseUrl}/cookies`, false);
let result = JSON.parse(xhr2.responseText);
assert.eq(result.cookies.key, undefined, 'cookies is omitted');

// With credentials: cookie should be sent back.
const xhr3 = await doXhr(`${baseUrl}/cookies`, true);
result = JSON.parse(xhr3.responseText);
assert.eq(result.cookies.key, cookieValue, 'cookies is same');

const cookies = new TextDecoder().decode(await tjs.readFile(cookieJarPath));
assert.ok(cookies.includes(cookieValue), 'cookies has wrote to disk');

server.close();
