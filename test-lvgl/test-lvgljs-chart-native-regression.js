import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

if (typeof lvgl.chartDebugGetState !== 'function' || typeof lvgl.chartDebugGetSeries !== 'function') {
    console.log('skip test-lvgljs-chart-native-regression: native chart debug APIs are not available');
    tjs.exit(0);
}

const STYLE_PART_INDICATOR = 0x020000;
const STYLE_PART_ITEMS = 0x050000;
const rgb24 = (value) => (Number(value) >>> 0) & 0x00ffffff;

const NC = lvgljs.default.NativeRender.NativeComponents;
const preNative = new NC.Chart({ uid: 'chart_native_reg_pre_native' });
preNative.setType(1);
preNative.setPointNum(4);
preNative.setDivLineCount([1, 2]);
preNative.setLeftAxisOption({
    majorLen: 0,
    minorLen: 0,
    majorNum: 4,
    minorNum: 1,
    drawSize: 40,
});
preNative.setLeftAxisData([{ data: [1, 2, 3, 4] }]);
assert.eq(preNative._children.length, 0,
    'native Chart props set before mount should not create fallback plot children');
assert.eq(preNative._plot, null,
    'native Chart props set before mount should not retain fallback plot');
assert.eq(lvgl.chartDebugGetSeries(preNative._native)?.length, 1,
    'pre-native Chart props should sync to native chart directly');
preNative.close();

const chart = new NC.Chart({ uid: 'chart_native_reg_chart' });

chart.setType(2);
chart.setPointNum(4);
chart.setDivLineCount([3, 5]);
chart.setLeftAxisOption({
    majorLen: 7,
    minorLen: 2,
    majorNum: 6,
    minorNum: 2,
    drawSize: 56,
    labelEn: true,
});
chart.setLeftAxisRange([0, 50]);
chart.setLeftAxisData([
    { color: 0x112233, data: [10, 20, 30, 40] },
    { color: 0x334455, data: [5, 15, 25, 35] },
]);
chart.setRightAxisData([
    { color: 0x778899, data: [1, 2, 3, 4] },
]);
chart.setLeftAxisLabels(['0', '10', '20', '30']);
chart.setBottomAxisLabels(['Jan', 'Feb', 'Mar', 'Apr']);

let st = lvgl.chartDebugGetState(chart._native);
assert.eq(st?.type, 2);
assert.eq(st?.pointCount, 4);
assert.eq(st?.hdiv, 3);
assert.eq(st?.vdiv, 5);
assert.eq(st?.ticks?.[0]?.majorLen, 7);
assert.eq(st?.ticks?.[0]?.drawSize, 56);
assert.eq(st?.axisLabels?.[0]?.[2], '20', 'left axis labels should reach native Chart state');
assert.eq(st?.axisLabels?.[2]?.[1], 'Feb', 'bottom axis labels should reach native Chart state');

chart.setLeftAxisRange([-10, 110]);
chart.setRightAxisRange([20, 220]);
chart.setBottomAxisRange([30, 330]);
chart.setTopAxisRange([40, 440]);
st = lvgl.chartDebugGetState(chart._native);
assert.eq(st?.ranges?.[0]?.[0], -10, 'left axis range should reach native primary-Y range');
assert.eq(st?.ranges?.[0]?.[1], 110);
assert.eq(st?.ranges?.[1]?.[0], 20, 'right axis range should reach native secondary-Y range');
assert.eq(st?.ranges?.[1]?.[1], 220);
assert.eq(st?.ranges?.[2]?.[0], 30, 'bottom axis range should reach native primary-X range');
assert.eq(st?.ranges?.[2]?.[1], 330);
assert.eq(st?.ranges?.[3]?.[0], 40, 'top axis range should reach native secondary-X range');
assert.eq(st?.ranges?.[3]?.[1], 440);

chart.setBottomAxisOption({
    majorLen: 3,
    minorLen: 1,
    majorNum: 5,
    minorNum: 1,
    drawSize: 31,
    labelEn: true,
});
chart.setRightAxisOption({
    majorLen: 4,
    minorLen: 1,
    majorNum: 6,
    minorNum: 1,
    drawSize: 32,
    labelEn: true,
});
chart.setTopAxisOption({
    majorLen: 5,
    minorLen: 1,
    majorNum: 7,
    minorNum: 1,
    drawSize: 33,
    labelEn: true,
});
st = lvgl.chartDebugGetState(chart._native);
assert.eq(st?.ticks?.[1]?.majorLen, 3, 'bottom axis option should rebuild native primary-X ticks');
assert.eq(st?.ticks?.[1]?.drawSize, 31);
assert.eq(st?.ticks?.[2]?.majorLen, 4, 'right axis option should rebuild native secondary-Y ticks');
assert.eq(st?.ticks?.[2]?.drawSize, 32);
assert.eq(st?.ticks?.[3]?.majorLen, 5, 'top axis option should rebuild native secondary-X ticks');
assert.eq(st?.ticks?.[3]?.drawSize, 33);

const defaultLabelChart = new NC.Chart({ uid: 'chart_native_reg_default_label_axis' });
defaultLabelChart.setLeftAxisOption({
    majorLen: 0,
    minorLen: 0,
    majorNum: 5,
    minorNum: 1,
    drawSize: 80,
});
defaultLabelChart.setBottomAxisOption({
    majorLen: 0,
    minorLen: 0,
    majorNum: 12,
    minorNum: 1,
    drawSize: 50,
});
st = lvgl.chartDebugGetState(defaultLabelChart._native);
assert.eq(st?.ticks?.[0]?.labelEn, true, 'left axis labels should default enabled like upstream lv_binding_js');
assert.eq(st?.ticks?.[1]?.labelEn, true, 'bottom axis labels should default enabled like upstream lv_binding_js');
defaultLabelChart.close();

assert.eq(typeof lvgl.debugGetStyle, 'function', 'native debugGetStyle should be exposed for style regressions');
chart.nativeSetStyle({
    'background-opacity': 64,
    'background-color': 0xabcdef,
    'border-color': 0x111111,
    'border-opacity': 65,
    'outline-color': 0x222222,
    'outline-opacity': 66,
    'shadow-color': 0x333333,
    'shadow-opacity': 67,
}, [
    'background-opacity',
    'background-color',
    'border-color',
    'border-opacity',
    'outline-color',
    'outline-opacity',
    'shadow-color',
    'shadow-opacity',
], 8, STYLE_PART_INDICATOR, false);
let partStyle = lvgl.debugGetStyle(chart._native, STYLE_PART_INDICATOR);
assert.eq(partStyle?.part, STYLE_PART_INDICATOR, 'indicatorStyle should map to LV_PART_INDICATOR');
assert.eq(rgb24(partStyle?.['background-color']), 0xabcdef);
assert.eq(partStyle?.['background-opacity'], 64, 'explicit background-opacity should not be overwritten by background-color');
assert.eq(rgb24(partStyle?.['border-color']), 0x111111);
assert.eq(partStyle?.['border-opacity'], 65);
assert.eq(rgb24(partStyle?.['outline-color']), 0x222222);
assert.eq(partStyle?.['outline-opacity'], 66);
assert.eq(rgb24(partStyle?.['shadow-color']), 0x333333);
assert.eq(partStyle?.['shadow-opacity'], 67);

chart.nativeSetStyle({
    'line-width': 6,
    'line-color': 0x123456,
    'line-rounded': true,
    'line-opacity': 123,
    'line-dash-width': 2,
    'line-dash-gap': 4,
}, [
    'line-width',
    'line-color',
    'line-rounded',
    'line-opacity',
    'line-dash-width',
    'line-dash-gap',
], 6, STYLE_PART_ITEMS, false);
partStyle = lvgl.debugGetStyle(chart._native, STYLE_PART_ITEMS);
assert.eq(partStyle?.part, STYLE_PART_ITEMS, 'itemStyle should map to LV_PART_ITEMS');
assert.eq(partStyle?.['line-width'], 6);
assert.eq(rgb24(partStyle?.['line-color']), 0x123456);
assert.eq(partStyle?.['line-rounded'], true);
assert.eq(partStyle?.['line-opacity'], 123);
assert.eq(partStyle?.['line-dash-width'], 2);
assert.eq(partStyle?.['line-dash-gap'], 4);

let series = lvgl.chartDebugGetSeries(chart._native);
assert.eq(series?.length, 3);
assert.eq(series?.[0]?.data?.length, 4);
assert.eq(series?.[1]?.data?.length, 4);
const rightAxisCount = (series || []).filter((it) => !!it?.yAxisSec).length;
assert.eq(rightAxisCount, 1, 'one merged series should be mapped to secondary Y axis');
const tails = [
    Number(series?.[0]?.data?.[3]) || 0,
    Number(series?.[1]?.data?.[3]) || 0,
    Number(series?.[2]?.data?.[3]) || 0,
].sort((a, b) => a - b);
assert.eq(tails[0], 4);
assert.eq(tails[1], 35);
assert.eq(tails[2], 40);

chart.setType(3);
chart.setScatterData([
    { color: 0x556677, data: [0, 11, 1, 22, 2, 33, 3, 44] },
]);
chart.setPointNum(4);

st = lvgl.chartDebugGetState(chart._native);
assert.eq(st?.type, 3);
assert.eq(st?.pointCount, 4);
series = lvgl.chartDebugGetSeries(chart._native);
assert.eq(series?.length, 1);
assert.eq(series?.[0]?.data2?.[1], 11);
assert.eq(series?.[0]?.data2?.[7], 44);

const derivedScatter = new NC.Chart({ uid: 'chart_native_reg_derived_scatter' });
derivedScatter.setType(3);
derivedScatter.setPointNum(3);
derivedScatter.setBottomAxisData([
    { data: [10, 20, 30] },
]);
derivedScatter.setLeftAxisData([
    { color: 0xaa00aa, data: [1, 2, 3] },
]);
series = lvgl.chartDebugGetSeries(derivedScatter._native);
assert.eq(series?.length, 1, 'derived scatter should emit one series');
const derivedColor = Number(series?.[0]?.color) >>> 0;
assert.eq(derivedColor & 0x00ffffff, 0xaa00aa, 'derived scatter should inherit Y axis series color');
assert.eq(series?.[0]?.data2?.[0], 10);
assert.eq(series?.[0]?.data2?.[1], 1);
assert.eq(series?.[0]?.data2?.[2], 20);
assert.eq(series?.[0]?.data2?.[3], 2);
assert.eq(series?.[0]?.data2?.[4], 30);
assert.eq(series?.[0]?.data2?.[5], 3);

derivedScatter.setBottomAxisData([
    { data: [40, 50, 60] },
]);
series = lvgl.chartDebugGetSeries(derivedScatter._native);
assert.eq(series?.[0]?.data2?.[0], 40, 'bottom axis data updates should rebuild derived scatter native X data');
assert.eq(series?.[0]?.data2?.[2], 50);
assert.eq(series?.[0]?.data2?.[4], 60);

const topDerivedScatter = new NC.Chart({ uid: 'chart_native_reg_top_scatter' });
topDerivedScatter.setType(3);
topDerivedScatter.setPointNum(2);
topDerivedScatter.setLeftAxisData([
    { color: 0x00bbcc, data: [7, 8] },
]);
topDerivedScatter.setTopAxisData([
    { data: [70, 80] },
]);
series = lvgl.chartDebugGetSeries(topDerivedScatter._native);
assert.eq(series?.length, 1, 'top-axis derived scatter should emit one series');
assert.eq(series?.[0]?.data2?.[0], 70, 'top axis data updates should rebuild derived scatter native X data');
assert.eq(series?.[0]?.data2?.[1], 7);
assert.eq(series?.[0]?.data2?.[2], 80);
assert.eq(series?.[0]?.data2?.[3], 8);
topDerivedScatter.close();

lvgl.setTheme({ primaryColor: 0x0a63c7, secondaryColor: 0x0a63c7 });
const themedDefault = new NC.Chart({ uid: 'chart_native_reg_theme_default_color' });
themedDefault.setType(1);
themedDefault.setPointNum(2);
themedDefault.setLeftAxisData([
    { color: -1, data: [11, 12] },
    { data: [13, 14] },
]);
series = lvgl.chartDebugGetSeries(themedDefault._native);
assert.eq(series?.length, 2, 'default-color chart should emit two series');
for (const item of series) {
    assert.eq((Number(item?.color) >>> 0) & 0x00ffffff, 0x0a63c7,
        'Chart color -1 or omitted should use the active theme primary color');
}
lvgl.setTheme({ primaryColor: 0x447799, secondaryColor: 0x447799 });
series = lvgl.chartDebugGetSeries(themedDefault._native);
for (const item of series) {
    assert.eq((Number(item?.color) >>> 0) & 0x00ffffff, 0x0a63c7,
        'Existing Chart series should keep creation-time theme color like upstream lv_binding_js');
}
themedDefault.setLeftAxisData([
    { color: -1, data: [15, 16] },
    { data: [17, 18] },
]);
series = lvgl.chartDebugGetSeries(themedDefault._native);
for (const item of series) {
    assert.eq((Number(item?.color) >>> 0) & 0x00ffffff, 0x447799,
        'Chart data rebuild should resolve default colors against the current theme');
}
themedDefault.close();
lvgl.resetTheme();

tjs.exit(0);
