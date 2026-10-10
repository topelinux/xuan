import assert from 'tjs:assert';

const order = [];

await new Promise(resolve => {
    setTimeout(() => {
        order.push('a');
        Promise.resolve().then(() => order.push('b'));
    }, 0);

    setTimeout(() => {
        order.push('c');
        Promise.resolve().then(() => {
            order.push('d');
            resolve();
        });
    }, 0);
});

assert.equal(order.join(','), 'a,b,c,d', 'microtasks should drain between timer callbacks');
