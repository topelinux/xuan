import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

const tab = new NC.TabView({ uid: 'tab_lifecycle_reg', tabPosition: 0, tabSize: 56 });
const p1 = new NC.View({ uid: 'tab_lifecycle_p1' });
const p2 = new NC.View({ uid: 'tab_lifecycle_p2' });
const p3 = new NC.View({ uid: 'tab_lifecycle_p3' });

tab.setTab('one', p1);
tab.setTab('two', p2);
tab.setTab('three', p3);

assert.eq(tab._pages.length, 3);
assert.eq(tab._tabButtons.length, 3);
assert.eq(tab._activeIndex, 0);

tab._tabButtons[2]._handleNativeEvent(7, null, null);
assert.eq(tab._activeIndex, 2, 'click third tab should activate index 2');
assert.eq(p3._styleCache.display, 'flex');

tab.removeChild(p2);
assert.eq(tab._pages.length, 2, 'remove middle page should shrink pages');
assert.eq(tab._tabButtons.length, 2, 'remove middle page should shrink tab buttons');
assert.eq(tab._activeIndex, 1, 'active index should be normalized to remaining last page');
assert.eq(p3._styleCache.display, 'flex');

tab._tabButtons[0]._handleNativeEvent(7, null, null);
assert.eq(tab._activeIndex, 0, 'click first remaining tab should switch to index 0');
assert.eq(p1._styleCache.display, 'flex');
assert.eq(p3._styleCache.display, 'none');

tab.removeChild(p1);
assert.eq(tab._pages.length, 1);
assert.eq(tab._tabButtons.length, 1);
assert.eq(tab._activeIndex, 0);
assert.eq(p3._styleCache.display, 'flex');

tab.removeChild(p3);
assert.eq(tab._pages.length, 0);
assert.eq(tab._tabButtons.length, 0);
assert.eq(tab._activeIndex, 0);

tjs.exit(0);
