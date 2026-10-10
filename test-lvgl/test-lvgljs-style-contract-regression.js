import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NativeView = lvgljs.default.NativeRender.NativeComponents.View;

class ContractProbe extends NativeView {
    constructor(opts) {
        super(opts);
        const inheritedStyle = super.style;
        assert.ok(inheritedStyle, 'super.style should exist for lvgljs-ui component contracts');
        assert.eq(typeof inheritedStyle.width, 'function');
        assert.eq(typeof inheritedStyle.height, 'function');
        this.style = new Proxy(this, {
            get(_obj, prop) {
                if (prop === 'width') {
                    return inheritedStyle.width.call(this);
                }
                if (prop === 'height') {
                    return inheritedStyle.height.call(this);
                }
                return undefined;
            },
        });
    }
}

const probe = new ContractProbe({ uid: 'style_contract_probe' });
probe.nativeSetStyle({ width: 320, height: 180 }, ['width', 'height'], 2, 0, true);
assert.eq(probe.style.width, 320);
assert.eq(probe.style.height, 180);

probe.close();
tjs.exit(0);
