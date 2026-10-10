const pathModule = globalThis[Symbol.for('tjs.internal.modules.path')];

export const basename = pathModule.basename;
export const dirname = pathModule.dirname;
export const extname = pathModule.extname;
export const format = pathModule.format;
export const isAbsolute = pathModule.isAbsolute;
export const join = pathModule.join;
export const normalize = pathModule.normalize;
export const parse = pathModule.parse;
export const relative = pathModule.relative;
export const resolve = pathModule.resolve;
export const sep = pathModule.sep;
export const delimiter = pathModule.delimiter;
export const posix = pathModule.posix;
export const win32 = pathModule.win32;
export const toNamespacedPath = pathModule.toNamespacedPath;

export default pathModule;
