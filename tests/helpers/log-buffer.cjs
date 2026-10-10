'use strict';

var ITEM_MAX_LENGTH = 1024;

function LogBuffer(max) {
    this.max = max || 64;
    this.clear();
}

LogBuffer.prototype.append = function (text) {
    if (text.length >= ITEM_MAX_LENGTH) {
        text = text.substr(0, ITEM_MAX_LENGTH);
    }
    this.log.shift();
    this.log.push([Date.now(), text]);
};

LogBuffer.prototype.fetch = function (options) {
    // fetch by count
    if (typeof options.latest === 'number') {
        var latest = options.latest;
        return this.log.slice(this.log.length - latest);
    }
    // fetch by timestamp
    var after = options.after || 0;
    for (var i = 0; i < this.log.length; ++i) {
        var item = this.log[i];
        if (item && item[0] > after) {
            return this.log.slice(i);
        }
    }
    return [];
};

LogBuffer.prototype.clear = function () {
    this.log = new Array(this.max);
};

module.exports = LogBuffer;

/*
function LogBuffer(max) {
    this.max = max || (1024 * 4);
    this.log = '';
}

LogBuffer.prototype.clear = function () {
    this.log = '';
}

LogBuffer.prototype.read = function () {
    return this.log;
}

LogBuffer.prototype.write = function (text) {
    var exceed = this.log.length + text.length - this.max;
    console.log('exceed:', exceed);
    if (exceed > 0) {
        // At least we must truncate to max
        this.log = this.log.substr(exceed);
        // If newline is found, we can truncate even more
        var newlinePos = this.log.indexOf('\n');
        if (newlinePos > 0) {
            this.log = this.log.substr(newlinePos + 1);
        }
    }
    this.log += text;
}

module.exports = LogBuffer;
*/
