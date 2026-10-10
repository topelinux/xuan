import assert from 'tjs:assert';
import EventEmitter, { EventEmitter as NamedEventEmitter } from 'events';
import { once, on, setMaxListeners, getMaxListeners, getEventListeners } from 'events';
import NodeEvents from 'node:events';

assert.eq(typeof EventEmitter, 'function');
assert.eq(EventEmitter, NamedEventEmitter);
assert.eq(typeof NodeEvents, 'function');
assert.eq(typeof once, 'function');
assert.eq(typeof on, 'function');

const ee = new EventEmitter();
const calls = [];

const onA = (v) => calls.push(`on:${v}`);
const onceA = (v) => calls.push(`once:${v}`);
const prependA = (v) => calls.push(`prepend:${v}`);

ee.on('a', onA);
ee.once('a', onceA);
ee.prependListener('a', prependA);

assert.eq(ee.listenerCount('a'), 3);
assert.eq(ee.emit('a', 1), true);
assert.eq(ee.emit('a', 2), true);
assert.eq(calls.join(','), 'prepend:1,on:1,once:1,prepend:2,on:2');
assert.eq(ee.listenerCount('a'), 2);

const listeners = ee.listeners('a');
const rawListeners = ee.rawListeners('a');
assert.eq(listeners.length, 2);
assert.eq(rawListeners.length, 2);
assert.eq(listeners[0], prependA);
assert.eq(listeners[1], onA);

ee.off('a', onA);
assert.eq(ee.listenerCount('a'), 1);
assert.eq(ee.eventNames().length, 1);
ee.removeAllListeners('a');
assert.eq(ee.listenerCount('a'), 0);
assert.eq(ee.eventNames().length, 0);
assert.eq(ee.emit('a', 3), false);

const nested = new EventEmitter();
const nestedCalls = [];
nested.once('outer', () => {
    nestedCalls.push('outer');
    nested.emit('inner');
});
nested.on('inner', () => nestedCalls.push('inner'));
assert.eq(nested.emit('outer'), true);
assert.eq(nested.emit('outer'), false);
assert.eq(nestedCalls.join(','), 'outer,inner');
assert.eq(nested.listenerCount('outer'), 0);

const p = once(ee, 'ready');
ee.emit('ready', 7, 'ok');
const readyArgs = await p;
assert.eq(readyArgs[0], 7);
assert.eq(readyArgs[1], 'ok');

const iterator = on(ee, 'tick');
ee.emit('tick', 11);
const tick1 = await iterator.next();
assert.eq(tick1.done, false);
assert.eq(tick1.value[0], 11);
await iterator.return();
assert.eq(ee.listenerCount('tick'), 0);

setMaxListeners(17);
const ee2 = new EventEmitter();
assert.eq(ee2.getMaxListeners(), 17);
setMaxListeners(3, ee2);
assert.eq(getMaxListeners(ee2), 3);
assert.eq(getEventListeners(ee, 'a').length, 0);

const cjsEventsNs = require('events');
const CjsEventEmitter = cjsEventsNs.EventEmitter ?? cjsEventsNs.default ?? cjsEventsNs;
assert.eq(cjsEventsNs.default, cjsEventsNs.EventEmitter);
const cjsEmitter = new CjsEventEmitter();
cjsEmitter.on('x', () => {});
assert.eq(cjsEmitter.listenerCount('x'), 1);
