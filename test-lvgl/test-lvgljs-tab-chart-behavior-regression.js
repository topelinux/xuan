import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

const tabs = new NC.TabView({ uid: 'tab_chart_reg_tabs', tabPosition: 0, tabSize: 60 });
const page1 = new NC.View({ uid: 'tab_chart_reg_page_1' });
const page2 = new NC.View({ uid: 'tab_chart_reg_page_2' });
tabs.setTab('One', page1);
tabs.setTab('Two', page2);

assert.eq(tabs._pages.length, 2);
assert.eq(tabs._tabButtons.length, 2);
assert.eq(tabs._activeIndex, 0);
assert.eq(tabs._children[0], tabs._header, 'top tabs should place header first');
assert.eq(tabs._children[1], tabs._content, 'top tabs should place content second');
assert.eq(tabs._content._styleCache['flex-grow'], 1);
assert.eq(page1._styleCache.display, 'flex');
assert.eq(page2._styleCache.display, 'none');

tabs._tabButtons[1]._handleNativeEvent(7, null, null);

assert.eq(tabs._activeIndex, 1);
assert.eq(page1._styleCache.display, 'none');
assert.eq(page2._styleCache.display, 'flex');
tabs.removeChild(page1);
assert.eq(tabs._pages.length, 1, 'removeChild should remove page entry');
assert.eq(tabs._tabButtons.length, 1, 'removeChild should remove tab button entry');
assert.eq(tabs._activeIndex, 0, 'active index should be normalized after removal');
assert.eq(page2._styleCache.display, 'flex', 'remaining page should stay visible');

const autoRoot = new NC.View({ uid: 'tab_chart_reg_auto_root' });
autoRoot._ensureNative();
autoRoot.nativeSetStyle(
    { width: 320, height: 180, padding: 0, 'border-width': 0 },
    ['width', 'height', 'padding', 'border-width'], 4, 0, true
);
const autoTabs = new NC.TabView({ uid: 'tab_chart_reg_auto_tabs', tabPosition: 1 << 2, tabSize: 50 });
autoRoot.appendChild(autoTabs);
const upstreamAuto = 2001 | (1 << 13);
autoTabs.nativeSetStyle({ width: upstreamAuto, height: upstreamAuto }, ['width', 'height'], 2, 0, false);
autoTabs.setTab('Auto', new NC.View({ uid: 'tab_chart_reg_auto_page' }));
if (typeof lvgl.debugGetGeometry === 'function') {
    const autoGeom = lvgl.debugGetGeometry(autoTabs._native);
    assert.eq(autoGeom?.width, 320, 'native TabView auto width should fill parent for widgets parity');
    assert.eq(autoGeom?.height, 180, 'native TabView auto height should fill parent for widgets parity');
}
autoRoot.close();

const bottomTabs = new NC.TabView({ uid: 'tab_chart_reg_tabs_bottom', tabPosition: 1 << 3, tabSize: 44 });
bottomTabs.setTab('A', new NC.View({ uid: 'tab_chart_reg_bottom_a' }));
bottomTabs.setTab('B', new NC.View({ uid: 'tab_chart_reg_bottom_b' }));
assert.eq(bottomTabs._children[0], bottomTabs._content, 'bottom tabs should place content first');
assert.eq(bottomTabs._children[1], bottomTabs._header, 'bottom tabs should place header second');

const leftTabs = new NC.TabView({ uid: 'tab_chart_reg_tabs_left', tabPosition: 1 << 0, tabSize: 72 });
const leftPage1 = new NC.View({ uid: 'tab_chart_reg_left_1' });
const leftPage2 = new NC.View({ uid: 'tab_chart_reg_left_2' });
leftTabs.setTab('L1', leftPage1);
leftTabs.setTab('L2', leftPage2);
assert.eq(leftTabs._children[0], leftTabs._header, 'left tabs should place header first');
assert.eq(leftTabs._children[1], leftTabs._content, 'left tabs should place content second');
assert.eq(leftTabs._header._styleCache.width, 72);
assert.eq(leftTabs._header._styleCache.height_pct, 100);
leftTabs.removeChild(leftPage1);
leftTabs._tabButtons[0]._handleNativeEvent(7, null, null);
assert.eq(leftTabs._activeIndex, 0, 'click index should stay stable after tab removal');

const rightTabs = new NC.TabView({ uid: 'tab_chart_reg_tabs_right', tabPosition: 1 << 1, tabSize: 68 });
rightTabs.setTab('R1', new NC.View({ uid: 'tab_chart_reg_right_1' }));
rightTabs.setTab('R2', new NC.View({ uid: 'tab_chart_reg_right_2' }));
assert.eq(rightTabs._children[0], rightTabs._content, 'right tabs should place content first');
assert.eq(rightTabs._children[1], rightTabs._header, 'right tabs should place header second');
assert.eq(rightTabs._header._styleCache.width, 68);

const chart = new NC.Chart({ uid: 'tab_chart_reg_chart' });
chart.setType(2);
chart.setDivLineCount([2, 4]);
chart.setPointNum(4);
chart.setLeftAxisOption({ majorLen: 6, minorLen: 2, majorNum: 5, minorNum: 2, drawSize: 48, labelEn: true });
chart.setLeftAxisData([
    { color: 0x00ff00, data: [10, 20, 30, 40] },
    { color: 0x0000ff, data: [8, 16, 24, 32] },
]);
chart.setLeftAxisRange([0, 40]);
assert.eq(chart._bars[0]?._nativeStyleCache.get(0)?.['background-color'], 0x00ff00, 'bar color should follow axis series color');
assert.eq(chart._bars[1]?._nativeStyleCache.get(0)?.['background-color'], 0x0000ff, 'second series color should be preserved');

assert.eq(chart._bars.length, 8, 'bar chart should render grouped bars for multi-series points');
assert.eq(chart._gridLines.length, 4, 'chart should build y-grid lines from divLineCount');
if (typeof lvgl.chartGetSeriesCount === 'function') {
    assert.eq(lvgl.chartGetSeriesCount(chart._native), 2, 'native chart should keep 2 active series');
    assert.eq(lvgl.chartGetPointNum(chart._native), 4, 'native chart point count should match');
}
if (typeof lvgl.chartDebugGetState === 'function') {
    const st = lvgl.chartDebugGetState(chart._native);
    assert.eq(st?.hdiv, 2, 'native chart hdiv should be applied');
    assert.eq(st?.vdiv, 4, 'native chart vdiv should be applied');
    assert.eq(st?.ticks?.[0]?.majorLen, 6, 'left axis major tick length should be applied');
    assert.eq(st?.ticks?.[0]?.drawSize, 48, 'left axis drawSize should be applied');
}
if (typeof lvgl.chartDebugGetSeries === 'function') {
    const nativeSeries = lvgl.chartDebugGetSeries(chart._native);
    assert.eq(nativeSeries?.length, 2, 'native chart should expose 2 series');
    const tails = (nativeSeries || []).map((s) => Number(s?.data?.[3]) || 0).sort((a, b) => a - b);
    assert.eq(tails?.[0], 32, 'native series should include second dataset tail 32');
    assert.eq(tails?.[1], 40, 'native series should include first dataset tail 40');
}

chart.setType(3);
chart.setScatterData([{ color: 0xff00ff, data: [0, 11, 1, 22, 2, 33, 3, 44] }]);
chart.setPointNum(4);
assert.eq(chart._bars.length, 4, 'scatter should map pairs to points');
assert.eq(chart._bars[0]?._nativeStyleCache.get(0)?.['background-color'], 0xff00ff, 'scatter color should follow series color');
if (typeof lvgl.chartDebugGetSeries === 'function') {
    const nativeSeries = lvgl.chartDebugGetSeries(chart._native);
    assert.eq(nativeSeries?.[0]?.data2?.[1], 11, 'scatter y point 0 should be 11');
    assert.eq(nativeSeries?.[0]?.data2?.[7], 44, 'scatter y point 3 should be 44');
}
chart.setBottomAxisLabels(['a', 'b', 'c']);
assert.eq(chart._bottomAxisLabels.length, 3, 'axis labels should be retained for renderer state');
chart.setPointNum(0);
chart.setScatterData([{ color: 0xff00ff, data: [0, 11, 1, 22] }]);
assert.eq(chart._bars.length, 3, 'labels length should participate in point count inference');
if (typeof lvgl.chartGetType === 'function') {
    assert.eq(lvgl.chartGetType(chart._native), 3, 'native chart type should switch to scatter');
}

tjs.exit(0);
