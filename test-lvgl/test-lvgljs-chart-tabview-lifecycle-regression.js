/**
 * Regression test for Chart/TabView lifecycle stability:
 *  - Chart: remove/re-add/close sequence, verify no double-delete
 *  - TabView: rapid remove/close cycles, verify _activeIndex consistency
 *  - Chart: _rebuildBars after partial dispose should be no-op
 */
import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

/* ── 1. Chart lifecycle: create → set data → remove → re-add → close ── */

const chartContainer = new NC.View({ uid: 'chart_lifecycle_container' });
chartContainer._ensureNative();
chartContainer.nativeSetStyle(
    { width: 200, height: 150 },
    ['width', 'height'], 2, 0, true
);

const chart = new NC.Chart({ uid: 'lifecycle_chart' });
chartContainer.appendChild(chart);
chart.setType(1); /* line chart */
chart.setPointNum(5);
chart.setLeftAxisData([{ data: [10, 20, 30, 40, 50], color: 0x2196f3 }]);
chart.setDivLineCount([3, 3]);

assert.eq(chart._disposed, false, 'chart should not be disposed');
assert.eq(chart._children.length > 0 || chart._nativeChartReady(), true,
    'chart should have children or be native-ready');

/* Remove and re-add */
chartContainer.removeChild(chart);
assert.eq(chart._disposed, true, 'chart should be disposed after removeChild');

/* Create a new chart and add to same container */
const chart2 = new NC.Chart({ uid: 'lifecycle_chart_2' });
chartContainer.appendChild(chart2);
chart2.setType(2); /* bar chart */
chart2.setPointNum(3);
chart2.setLeftAxisData([{ data: [5, 15, 25], color: 0x4caf50 }]);

assert.eq(chart2._disposed, false, 'chart2 should not be disposed');

/* Call _rebuildBars on chart2 — should work fine */
chart2._rebuildBars();

chartContainer.removeChild(chart2);
chartContainer.close();

/* ── 2. Chart _rebuildBars after dispose is a no-op ─────────── */

const chart3 = new NC.Chart({ uid: 'disposed_chart' });
chart3._ensureNative();
chart3.close();

assert.eq(chart3._disposed, true, 'chart3 should be disposed after close');
/* Calling _rebuildBars on disposed chart should be a safe no-op */
chart3._rebuildBars();
chart3.setType(1);
chart3.setPointNum(10);
chart3.setLeftAxisData([{ data: [1, 2, 3] }]);

/* ── 3. TabView rapid remove/close cycles ───────────────────── */

const tabContainer = new NC.View({ uid: 'tab_lifecycle_container' });
tabContainer._ensureNative();
tabContainer.nativeSetStyle(
    { width: 300, height: 200 },
    ['width', 'height'], 2, 0, true
);

const tabView = new NC.TabView({ uid: 'lifecycle_tabview' });
tabContainer.appendChild(tabView);

const pages = [];
const pageNames = ['Tab A', 'Tab B', 'Tab C', 'Tab D'];

for (const name of pageNames) {
    const page = new NC.View({ uid: `page_${name}` });
    page._ensureNative();
    tabView.setTab(name, page);
    pages.push(page);
}

assert.eq(tabView._pages.length, 4, 'tabview should have 4 pages');
assert.eq(tabView._tabButtons.length, 4, 'tabview should have 4 tab buttons');
assert.eq(tabView._activeIndex, 0, 'initial active index should be 0');

/* Remove middle tab */
tabView.removeChild(pages[1]);
assert.eq(tabView._pages.length, 3, 'should have 3 pages after removing Tab B');
assert.eq(tabView._tabButtons.length, 3, 'should have 3 tab buttons after removing Tab B');
assert.eq(tabView._activeIndex, 0, 'active index should still be 0');

/* Remove first tab (active) */
tabView.removeChild(pages[0]);
assert.eq(tabView._pages.length, 2, 'should have 2 pages after removing Tab A');
assert.eq(tabView._activeIndex, 0, 'active index should normalize to 0');

/* Add a new tab */
const newPage = new NC.View({ uid: 'page_new' });
newPage._ensureNative();
tabView.setTab('Tab E', newPage);
assert.eq(tabView._pages.length, 3, 'should have 3 pages after adding Tab E');

/* Close the tabView — should clean up all pages and buttons safely */
tabView.close();

/* ── 4. Chart with native data then close ───────────────────── */

const chart4 = new NC.Chart({ uid: 'native_close_chart' });
chart4._ensureNative();
chart4.setType(1);
chart4.setPointNum(10);
chart4.setLeftAxisData([
    { data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], color: 0xff5722 }
]);

/* If native chart is available, verify series count */
if (chart4._nativeChartReady() && typeof lvgl.chartGetSeriesCount === 'function') {
    const count = lvgl.chartGetSeriesCount(chart4._native);
    assert.eq(count >= 1, true, 'native chart should have at least 1 series');
}

/* Close should not crash even with native chart series allocated */
chart4.close();
assert.eq(chart4._disposed, true, 'chart4 should be disposed');

/* ── 5. Multi-round Chart remove/re-add cycle ──────────────── */

const roundContainer = new NC.View({ uid: 'round_container' });
roundContainer._ensureNative();
roundContainer.nativeSetStyle(
    { width: 200, height: 150 },
    ['width', 'height'], 2, 0, true
);

for (let round = 0; round < 3; round++) {
    const c = new NC.Chart({ uid: `round_chart_${round}` });
    roundContainer.appendChild(c);
    c.setType(2);
    c.setPointNum(4);
    c.setLeftAxisData([{ data: [round * 10, round * 20, round * 30, round * 40], color: 0x9c27b0 }]);
    c.setDivLineCount([2, 2]);

    assert.eq(c._disposed, false, `round ${round}: chart should be alive`);
    roundContainer.removeChild(c);
    assert.eq(c._disposed, true, `round ${round}: chart should be disposed after remove`);
    assert.eq(roundContainer._children.length, 0, `round ${round}: container should be empty`);
}
roundContainer.close();

/* ── 6. Post-dispose operations on Chart are safe no-ops ────── */

const postChart = new NC.Chart({ uid: 'post_dispose_chart' });
postChart._ensureNative();
postChart.setType(1);
postChart.setPointNum(5);
postChart.setLeftAxisData([{ data: [1, 2, 3, 4, 5] }]);
postChart.close();

assert.eq(postChart._disposed, true, 'postChart should be disposed');

/* All of these must be safe no-ops, not crashes */
postChart.setType(2);
postChart.setPointNum(100);
postChart.setDivLineCount([5, 5]);
postChart.setLeftAxisData([{ data: [99, 99] }]);
postChart.setLeftAxisOption({ majorLen: 5 });
postChart.setRightAxisData([{ data: [1, 2] }]);
postChart.setScatterData([{ data: [1, 2, 3, 4] }]);
postChart._rebuildBars();
postChart._applyAxisTickNative(0, { majorLen: 3, minorLen: 1, majorNum: 4, minorNum: 1, drawSize: 50 });
postChart._ensurePlot();

/* Verify no state corruption: new chart in same test should work fine */
const afterChart = new NC.Chart({ uid: 'after_post_chart' });
afterChart._ensureNative();
afterChart.setType(1);
afterChart.setPointNum(3);
afterChart.setLeftAxisData([{ data: [7, 8, 9] }]);
assert.eq(afterChart._disposed, false, 'afterChart should work after post-dispose ops');
afterChart.close();

/* ── 7. Post-dispose operations on TabView are safe no-ops ──── */

const postTab = new NC.TabView({ uid: 'post_dispose_tabview' });
postTab._ensureNative();
postTab.close();

assert.eq(postTab._disposed, true, 'postTab should be disposed');

/* These must be safe no-ops */
const deadPage = new NC.View({ uid: 'dead_page' });
deadPage._ensureNative();
postTab.setTab('Ghost', deadPage);
assert.eq(postTab._pages.length, 0, 'setTab on disposed should not add pages');

postTab.removeChild(deadPage); /* should not crash */

/* Verify no state pollution */
const freshTab = new NC.TabView({ uid: 'fresh_tabview' });
freshTab._ensureNative();
const freshPage = new NC.View({ uid: 'fresh_page' });
freshPage._ensureNative();
freshTab.setTab('Alive', freshPage);
assert.eq(freshTab._pages.length, 1, 'fresh tabview should accept tabs after post-dispose test');
assert.eq(freshTab._activeIndex, 0, 'fresh tabview activeIndex should be 0');
freshTab.close();

/* ── 8. Post-dispose nativeSetStyle / appendChild are no-ops ── */

const styleGuard = new NC.View({ uid: 'style_guard_view' });
styleGuard._ensureNative();
styleGuard.close();

assert.eq(styleGuard._disposed, true, 'styleGuard should be disposed');

/* nativeSetStyle on disposed object should not crash */
styleGuard.nativeSetStyle(
    { width: 999, height: 999, 'background-color': 0xff0000 },
    ['width', 'height', 'background-color'], 3, 0, true
);

/* appendChild on disposed parent should not crash */
const orphan = new NC.View({ uid: 'orphan_child' });
orphan._ensureNative();
styleGuard.appendChild(orphan);
assert.eq(styleGuard._children.length, 0, 'disposed parent should not accept children');

/* insertChildBefore on disposed should not crash */
styleGuard.insertChildBefore(orphan, null);

/* Double-close should be safe */
styleGuard.close();
assert.eq(styleGuard._disposed, true, 'double close should still show disposed');
orphan.close();

/* ── 9. TabView: dispose after partial structure ────────────── */

const partialTab = new NC.TabView({ uid: 'partial_tabview' });
partialTab._ensureNative();
const p1 = new NC.View({ uid: 'partial_p1' });
p1._ensureNative();
partialTab.setTab('One', p1);
assert.eq(partialTab._pages.length, 1, 'partial tab should have 1 page');

/* Close with only 1 tab — exercises edge case in _setActiveIndex */
partialTab.close();
assert.eq(partialTab._disposed, true, 'partial tab should be disposed');

tjs.exit(0);
