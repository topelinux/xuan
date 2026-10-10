import assert from 'tjs:assert';

const redirectUrl = 'https://postman-echo.com/redirect-to?url=https://postman-echo.com/get';

const xhr = new XMLHttpRequest();
xhr.open('GET', redirectUrl);
xhr.onloadend = () => {
    assert.eq(xhr.readyState, xhr.DONE, 'readyState is DONE');
    assert.eq(xhr.status, 200, 'status is 200');
    assert.ok(xhr.responseURL.includes('postman-echo.com/get'), 'url was redirected');
    assert.ok(xhr.responseText.length > 0, 'response body is not empty');
};
xhr.send();
