/**
 * Regression test for:
 *  - overflow style mapping (hidden/scroll/auto → LV_OBJ_FLAG_SCROLLABLE)
 *  - width/height auto → LV_SIZE_CONTENT via native width_content/height_content
 *  - grid template with auto/fr mixed tracks
 */
import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

/* ── 1. Overflow mapping ────────────────────────────────────── */

const overflowView = new NC.View({ uid: 'overflow_test' });
overflowView._ensureNative();

/* Default: objects are scrollable */
assert.eq(
    lvgl.debugGetScrollable ? lvgl.debugGetScrollable(overflowView._native) : true,
    true,
    'default view should be scrollable (LV_OBJ_FLAG_SCROLLABLE set)'
);

/* overflow: hidden → value 1 → clear SCROLLABLE */
overflowView.nativeSetStyle({ overflow: 1 }, ['overflow'], 1, 0, false);
if (typeof lvgl.debugGetScrollable === 'function') {
    assert.eq(lvgl.debugGetScrollable(overflowView._native), false, 'overflow hidden should clear scrollable flag');
}

/* overflow: scroll/auto → value 0 → set SCROLLABLE */
overflowView.nativeSetStyle({ overflow: 0 }, ['overflow'], 1, 0, false);
if (typeof lvgl.debugGetScrollable === 'function') {
    assert.eq(lvgl.debugGetScrollable(overflowView._native), true, 'overflow scroll/auto should set scrollable flag');
}

/* string form compatibility for direct style calls */
overflowView.nativeSetStyle({ overflow: 'hidden' }, ['overflow'], 1, 0, false);
if (typeof lvgl.debugGetScrollable === 'function') {
    assert.eq(lvgl.debugGetScrollable(overflowView._native), false, 'overflow "hidden" string should clear scrollable flag');
}
overflowView.nativeSetStyle({ overflow: 'auto' }, ['overflow'], 1, 0, false);
if (typeof lvgl.debugGetScrollable === 'function') {
    assert.eq(lvgl.debugGetScrollable(overflowView._native), true, 'overflow "auto" string should set scrollable flag');
}

overflowView.close();

/* ── 2. Width/height auto → LV_SIZE_CONTENT ─────────────────── */

const autoSizeView = new NC.View({ uid: 'autosize_test' });
autoSizeView._ensureNative();

/* Simulate what the upstream style pipe sends for width: 'auto' */
const AUTO_SIZE_MARKER = 2001 | (1 << 13); /* LV_SIZE_CONTENT encoding from upstream style pipe */
autoSizeView.nativeSetStyle(
    { width: AUTO_SIZE_MARKER, height: AUTO_SIZE_MARKER },
    ['width', 'height'], 2, 0, true
);

/* Verify the native style cache contains content keys */
let foundWidthContent = false;
let foundHeightContent = false;
for (const [, bucket] of autoSizeView._nativeStyleCache) {
    if (bucket['width_content'] === 1) foundWidthContent = true;
    if (bucket['height_content'] === 1) foundHeightContent = true;
}
assert.eq(foundWidthContent, true, 'width auto should produce width_content key');
assert.eq(foundHeightContent, true, 'height auto should produce height_content key');

autoSizeView.close();

/* ── 3. Grid template with auto + fr mixed tracks ───────────── */

const gridContainer = new NC.View({ uid: 'grid_test' });
gridContainer._ensureNative();

/* Upstream grid pipe produces arrays with LVGL constants directly:
 *   auto → LV_GRID_CONTENT = 8090
 *   1fr  → LV_GRID_FR(1)  = 8092
 *   2fr  → LV_GRID_FR(2)  = 8093
 *   plain → pixel value (e.g., 20)
 */
const LV_GRID_CONTENT = (1 << 13) - 1 - 101; /* 8090 */
const LV_GRID_FR_1 = (1 << 13) - 1 - 100 + 1; /* 8092 = LV_GRID_FR(1) */

gridContainer.nativeSetStyle(
    {
        display: 'grid',
        'grid-template': [
            [LV_GRID_FR_1, LV_GRID_FR_1],         /* 1fr 1fr */
            [LV_GRID_CONTENT, LV_GRID_CONTENT],     /* auto auto */
        ],
        'grid-align': [0, 0], /* start, start */
    },
    ['display', 'grid-template', 'grid-align'], 3, 0, true
);
if (typeof lvgl.__debugGetGridTemplate === 'function') {
    const tpl = lvgl.__debugGetGridTemplate(gridContainer._native);
    assert.eq(Number(tpl?.cols?.[0]), LV_GRID_FR_1, 'grid cols[0] should keep FR marker');
    assert.eq(Number(tpl?.rows?.[0]), LV_GRID_CONTENT, 'grid rows[0] should keep CONTENT marker');
}

/* Add grid children */
const gridChild1 = new NC.View({ uid: 'grid_child_1' });
gridChild1._ensureNative();
gridChild1.nativeSetStyle(
    { 'grid-child': [0, 0, 1, 0, 0, 1] }, /* start, col=0, span=1, start, row=0, span=1 */
    ['grid-child'], 1, 0, true
);
gridContainer.appendChild(gridChild1);

const gridChild2 = new NC.View({ uid: 'grid_child_2' });
gridChild2._ensureNative();
gridChild2.nativeSetStyle(
    { 'grid-child': [0, 1, 1, 0, 0, 1] }, /* start, col=1, span=1, start, row=0, span=1 */
    ['grid-child'], 1, 0, true
);
gridContainer.appendChild(gridChild2);

/* If we reach here without crash, grid layout is stable */
assert.eq(gridContainer._children.length, 2, 'grid container should have 2 children');

/* ── 3a. Grid cell observability (debugGetGridCell) ─────────── */
if (typeof lvgl.debugGetGridCell === 'function') {
    const cell1 = lvgl.debugGetGridCell(gridChild1._native);
    assert.eq(Number(cell1.colPos), 0, 'gridChild1 colPos should be 0');
    assert.eq(Number(cell1.colSpan), 1, 'gridChild1 colSpan should be 1');
    assert.eq(Number(cell1.rowPos), 0, 'gridChild1 rowPos should be 0');
    assert.eq(Number(cell1.rowSpan), 1, 'gridChild1 rowSpan should be 1');
    assert.eq(Number(cell1.xAlign), 0, 'gridChild1 xAlign should be start (0)');
    assert.eq(Number(cell1.yAlign), 0, 'gridChild1 yAlign should be start (0)');

    const cell2 = lvgl.debugGetGridCell(gridChild2._native);
    assert.eq(Number(cell2.colPos), 1, 'gridChild2 colPos should be 1');
    assert.eq(Number(cell2.colSpan), 1, 'gridChild2 colSpan should be 1');
    assert.eq(Number(cell2.rowPos), 0, 'gridChild2 rowPos should be 0');
    assert.eq(Number(cell2.rowSpan), 1, 'gridChild2 rowSpan should be 1');
}

/* Clean up grid children */
gridContainer.removeChild(gridChild1);
gridContainer.removeChild(gridChild2);
assert.eq(gridContainer._children.length, 0, 'grid container should have 0 children after remove');

gridContainer.close();

/* ── 4. Mixed auto + fr grid (profile/analytics pattern) ────── */

const LV_GRID_FR_2 = (1 << 13) - 1 - 100 + 2; /* 8093 = LV_GRID_FR(2) */

const mixedGrid = new NC.View({ uid: 'mixed_grid' });
mixedGrid._ensureNative();

/* Pattern: 'auto 5 1fr 1fr' for columns, '1fr 1fr' for rows */
mixedGrid.nativeSetStyle(
    {
        display: 'grid',
        'grid-template': [
            [LV_GRID_CONTENT, 5, LV_GRID_FR_1, LV_GRID_FR_1], /* auto 5 1fr 1fr */
            [LV_GRID_FR_1, LV_GRID_FR_1],                      /* 1fr 1fr */
        ],
        'grid-align': [0, 0],
    },
    ['display', 'grid-template', 'grid-align'], 3, 0, true
);
if (typeof lvgl.__debugGetGridTemplate === 'function') {
    const tpl = lvgl.__debugGetGridTemplate(mixedGrid._native);
    assert.eq(Number(tpl?.cols?.[0]), LV_GRID_CONTENT, 'mixed grid col[0] should keep CONTENT marker');
    assert.eq(Number(tpl?.cols?.[1]), 5, 'mixed grid col[1] should keep fixed track');
    assert.eq(Number(tpl?.cols?.[2]), LV_GRID_FR_1, 'mixed grid col[2] should keep FR marker');
}

/* Add children to each cell */
const cells = [];
for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 2; row++) {
        const cell = new NC.View({ uid: `cell_${col}_${row}` });
        cell._ensureNative();
        cell.nativeSetStyle(
            { 'grid-child': [0, col, 1, 0, row, 1] },
            ['grid-child'], 1, 0, true
        );
        mixedGrid.appendChild(cell);
        cells.push(cell);
    }
}

assert.eq(mixedGrid._children.length, 8, 'mixed grid should have 8 children');

/* Remove all and verify no crash */
for (const cell of cells) {
    mixedGrid.removeChild(cell);
}
assert.eq(mixedGrid._children.length, 0, 'mixed grid should be empty after cleanup');

mixedGrid.close();

/* ── 5. Grid cell with span + align variations ──────────────── */
if (typeof lvgl.debugGetGridCell === 'function') {
    const spanGrid = new NC.View({ uid: 'span_grid' });
    spanGrid._ensureNative();
    spanGrid.nativeSetStyle(
        {
            display: 'grid',
            'grid-template': [
                [LV_GRID_FR_1, LV_GRID_FR_1, LV_GRID_FR_1],
                [LV_GRID_FR_1, LV_GRID_FR_1],
            ],
            'grid-align': [0, 0],
        },
        ['display', 'grid-template', 'grid-align'], 3, 0, true
    );

    /* Child spanning 2 columns (col=0, span=2) in row 0 */
    const spanningChild = new NC.View({ uid: 'span_child' });
    spanningChild._ensureNative();
    spanningChild.nativeSetStyle(
        { 'grid-child': [0, 0, 2, 0, 0, 1] }, /* start, col=0, span=2, start, row=0, span=1 */
        ['grid-child'], 1, 0, true
    );
    spanGrid.appendChild(spanningChild);

    const spanCell = lvgl.debugGetGridCell(spanningChild._native);
    assert.eq(Number(spanCell.colPos), 0, 'spanningChild colPos should be 0');
    assert.eq(Number(spanCell.colSpan), 2, 'spanningChild colSpan should be 2');
    assert.eq(Number(spanCell.rowPos), 0, 'spanningChild rowPos should be 0');
    assert.eq(Number(spanCell.rowSpan), 1, 'spanningChild rowSpan should be 1');

    /* Child with center align in row 1 */
    const centeredChild = new NC.View({ uid: 'center_child' });
    centeredChild._ensureNative();
    centeredChild.nativeSetStyle(
        { 'grid-child': [1, 1, 1, 1, 1, 1] }, /* center, col=1, span=1, center, row=1, span=1 */
        ['grid-child'], 1, 0, true
    );
    spanGrid.appendChild(centeredChild);

    const centerCell = lvgl.debugGetGridCell(centeredChild._native);
    assert.eq(Number(centerCell.colPos), 1, 'centeredChild colPos should be 1');
    assert.eq(Number(centerCell.colSpan), 1, 'centeredChild colSpan should be 1');
    assert.eq(Number(centerCell.rowPos), 1, 'centeredChild rowPos should be 1');
    assert.eq(Number(centerCell.rowSpan), 1, 'centeredChild rowSpan should be 1');
    assert.eq(Number(centerCell.xAlign), 1, 'centeredChild xAlign should be center (1)');
    assert.eq(Number(centerCell.yAlign), 1, 'centeredChild yAlign should be center (1)');

    spanGrid.close();
}

/* ── 6. Grid cell observability after removeChild + re-add ──── */
if (typeof lvgl.debugGetGridCell === 'function') {
    const readdGrid = new NC.View({ uid: 'readd_grid' });
    readdGrid._ensureNative();
    readdGrid.nativeSetStyle(
        {
            display: 'grid',
            'grid-template': [[LV_GRID_FR_1, LV_GRID_FR_1], [LV_GRID_FR_1]],
            'grid-align': [0, 0],
        },
        ['display', 'grid-template', 'grid-align'], 3, 0, true
    );

    const child = new NC.View({ uid: 'readd_child' });
    child._ensureNative();
    child.nativeSetStyle(
        { 'grid-child': [0, 1, 1, 0, 0, 1] },
        ['grid-child'], 1, 0, true
    );
    readdGrid.appendChild(child);

    let cell = lvgl.debugGetGridCell(child._native);
    assert.eq(Number(cell.colPos), 1, 'before remove: colPos should be 1');

    readdGrid.removeChild(child);
    /* child is now disposed — create fresh */
    const child2 = new NC.View({ uid: 'readd_child_2' });
    child2._ensureNative();
    child2.nativeSetStyle(
        { 'grid-child': [0, 0, 1, 0, 0, 1] },
        ['grid-child'], 1, 0, true
    );
    readdGrid.appendChild(child2);

    cell = lvgl.debugGetGridCell(child2._native);
    assert.eq(Number(cell.colPos), 0, 'after re-add: colPos should be 0');
    assert.eq(Number(cell.rowPos), 0, 'after re-add: rowPos should be 0');

    readdGrid.close();
}

/* ── 7. Nested grid/flex parity for widgets-like layouts ────── */
if (typeof lvgl.__debugGetGridTemplate === 'function' && typeof lvgl.debugGetGridCell === 'function') {
    const nestedRoot = new NC.View({ uid: 'nested_layout_root' });
    nestedRoot._ensureNative();
    nestedRoot.nativeSetStyle(
        {
            display: 'grid',
            width: 320,
            height: 240,
            'grid-template': [
                [LV_GRID_CONTENT, LV_GRID_FR_1],
                [LV_GRID_FR_1, LV_GRID_CONTENT],
            ],
            'grid-align': [0, 0],
        },
        ['display', 'width', 'height', 'grid-template', 'grid-align'], 5, 0, true
    );

    const nestedFlex = new NC.View({ uid: 'nested_layout_flex' });
    nestedFlex._ensureNative();
    nestedFlex.nativeSetStyle(
        {
            display: 'flex',
            'flex-flow': 1,
            'grid-child': [0, 1, 1, 0, 0, 2],
            width_pct: 100,
            height_pct: 100,
            'row-spacing': 4,
        },
        ['display', 'flex-flow', 'grid-child', 'width_pct', 'height_pct', 'row-spacing'], 6, 0, true
    );
    nestedRoot.appendChild(nestedFlex);

    const nestedButton = new NC.Button({ uid: 'nested_layout_button' });
    nestedButton._ensureNative();
    nestedButton.nativeSetStyle(
        {
            width_pct: 100,
            height: 32,
            'border-radius': 8,
        },
        ['width_pct', 'height', 'border-radius'], 3, 0, true
    );
    nestedFlex.appendChild(nestedButton);

    const rootTpl = lvgl.__debugGetGridTemplate(nestedRoot._native);
    assert.eq(Number(rootTpl?.cols?.[0]), LV_GRID_CONTENT, 'nested root should keep CONTENT col');
    assert.eq(Number(rootTpl?.cols?.[1]), LV_GRID_FR_1, 'nested root should keep FR col');
    assert.eq(Number(rootTpl?.rows?.[1]), LV_GRID_CONTENT, 'nested root should keep CONTENT row');

    const nestedCell = lvgl.debugGetGridCell(nestedFlex._native);
    assert.eq(Number(nestedCell.colPos), 1, 'nested flex should be in col 1');
    assert.eq(Number(nestedCell.rowPos), 0, 'nested flex should start at row 0');
    assert.eq(Number(nestedCell.rowSpan), 2, 'nested flex should span two rows');
    assert.eq(nestedFlex._children.length, 1, 'nested flex should keep child ownership');

    nestedRoot.close();
}

/* ── 8. Grid stretch cell mapping ───────────────────────────── */
if (typeof lvgl.debugGetGridCell === 'function') {
    const stretchGrid = new NC.View({ uid: 'stretch_grid' });
    stretchGrid._ensureNative();
    stretchGrid.nativeSetStyle(
        {
            display: 'grid',
            'grid-template': [[LV_GRID_FR_1], [LV_GRID_FR_1]],
            'grid-align': [0, 0],
        },
        ['display', 'grid-template', 'grid-align'], 3, 0, true
    );

    const stretchChild = new NC.View({ uid: 'stretch_child' });
    stretchChild._ensureNative();
    stretchChild.nativeSetStyle(
        { 'grid-child': [3, 0, 1, 3, 0, 1] }, /* stretch x/y */
        ['grid-child'], 1, 0, true
    );
    stretchGrid.appendChild(stretchChild);

    const stretchCell = lvgl.debugGetGridCell(stretchChild._native);
    assert.eq(Number(stretchCell.xAlign), 3, 'stretch child xAlign should be stretch (3)');
    assert.eq(Number(stretchCell.yAlign), 3, 'stretch child yAlign should be stretch (3)');

    stretchGrid.close();
}

tjs.exit(0);
