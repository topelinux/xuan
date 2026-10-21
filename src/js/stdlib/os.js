function getFamily(address = '') {
    return address.includes(':') ? 'IPv6' : 'IPv4';
}

function countBits(part) {
    let bits = 0;
    while (part) {
        bits += part & 1;
        part >>>= 1;
    }
    return bits;
}

function netmaskToPrefix(netmask, family) {
    if (!netmask) return null;
    if (family === 'IPv4') {
        return netmask.split('.').reduce((total, part) => total + countBits(Number(part) || 0), 0);
    }
    return netmask
        .split(':')
        .reduce((total, part) => total + countBits(parseInt(part || '0', 16) || 0), 0);
}

function toCidr(address, netmask, family) {
    const prefix = netmaskToPrefix(netmask, family);
    if (prefix == null || !address) return null;
    return `${address}/${prefix}`;
}

function normalizeInterface(entry) {
    const family = getFamily(entry.address);
    const normalized = {
        address: entry.address,
        netmask: entry.netmask,
        family,
        mac: entry.mac,
        internal: entry.internal,
        cidr: toCidr(entry.address, entry.netmask, family)
    };

    if (family === 'IPv6' && entry.scopeId != null) {
        normalized.scopeid = entry.scopeId;
    }

    return normalized;
}

function networkInterfaces() {
    const entries = tjs.system.networkInterfaces;
    const interfaces = {};

    for (const entry of entries) {
        if (!entry || !entry.name) continue;
        if (!interfaces[entry.name]) {
            interfaces[entry.name] = [];
        }
        interfaces[entry.name].push(normalizeInterface(entry));
    }

    return interfaces;
}

function cpus() {
    const cpus = tjs.system.cpus;
    return Array.isArray(cpus) ? cpus : [];
}

function totalmem() {
    const n = tjs.system.totalMem;
    return Number.isFinite(n) ? n : 0;
}

function freemem() {
    const n = tjs.system.freeMem;
    return Number.isFinite(n) ? n : 0;
}

function uptime() {
    const n = tjs.system.uptime;
    return Number.isFinite(n) ? n : 0;
}

function homedir() { return tjs.homeDir; }
function tmpdir() { return tjs.tmpDir; }
const os = { cpus, totalmem, freemem, networkInterfaces, uptime, homedir, tmpdir };

export { cpus, totalmem, freemem, uptime };
export { networkInterfaces };
export { homedir, tmpdir };
export default os;
