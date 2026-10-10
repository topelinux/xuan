const core = globalThis[Symbol.for('tjs.internal.core')];
const util = core.util;

if (util.inspect.defaultOptions == undefined) {
    util.inspect.defaultOptions = {}
}
export const format = util.format;
export const formatWithOptions = util.formatWithOptions;
export const inspect = util.inspect;
export const deprecate = util.deprecate;
export const debuglog = util.debuglog;
export const inherits = util.inherits;
export const getSystemErrorName = util.getSystemErrorName;

export const isArray = util.isArray;
export const isBoolean = util.isBoolean;
export const isNull = util.isNull;
export const isNullOrUndefined = util.isNullOrUndefined;
export const isNumber = util.isNumber;
export const isString = util.isString;
export const isSymbol = util.isSymbol;
export const isUndefined = util.isUndefined;
export const isObject = util.isObject;
export const isFunction = util.isFunction;
export const isRegExp = util.isRegExp;
export const isDate = util.isDate;
export const isError = util.isError;
export const isPrimitive = util.isPrimitive;

export const types = util.types;

function promisify(original) {
    if (typeof original !== 'function') {
        throw new TypeError('The "original" argument must be a function');
    }

    const promisified = function (...args) {
        return new Promise((resolve, reject) => {
            original.call(this, ...args, (err, ...values) => {
                if (err) {
                    reject(err);
                } else {
                    resolve(values.length === 1 ? values[0] : values);
                }
            });
        });
    };

    Object.setPrototypeOf(promisified, Object.getPrototypeOf(original));
    Object.defineProperties(promisified, Object.getOwnPropertyDescriptors(original));

    promisified[Symbol.for('util.promisify.custom')] = original[Symbol.for('util.promisify.custom')] || original;

    return promisified;
}

export { promisify };
export default util;
