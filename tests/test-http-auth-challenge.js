import assert from 'tjs:assert';
import http from 'node:http';

// Basic and unconfigured Digest challenges must reach the JS caller as 401.
const server = http.createServer((req, res) => {
    res.writeHead(401, {'Content-Type':'application/json',
        'WWW-Authenticate':req.url === '/digest' ? 'Digest realm="test", nonce="test"' : 'Basic realm="test"'});
    res.end('{"error":"Authentication required"}');
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const root = 'http://127.0.0.1:' + server.address().port;
try {
    for (const route of ['/basic','/digest']) {
        const response = await fetch(root + route);
        assert.eq(response.status,401);
        assert.eq((await response.json()).error,'Authentication required');
        await new Promise((resolve,reject)=>{
            http.get(root + route,res=>{
                let text='';res.on('data',b=>text+=b);res.on('error',reject);
                res.on('end',()=>{
                    try { assert.eq(res.statusCode,401);assert.eq(JSON.parse(text).error,'Authentication required');resolve(); }
                    catch(e){reject(e);}
                });
            }).on('error',reject);
        });
    }
    console.log('HTTP Basic and unconfigured Digest 401 responses passed');
} finally { await new Promise(resolve=>server.close(resolve)); }
