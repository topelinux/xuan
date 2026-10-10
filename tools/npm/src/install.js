import { promises as fsp } from 'tjs:fs';
import path from 'tjs:path';
import https from 'tjs:https';
import { createGunzipStream, gunzipStreamPush, gunzipStreamFinish } from 'tjs:zlib';

const DEFAULT_REGISTRY = 'https://registry.npmjs.org';
const DEFAULT_CONCURRENCY = 4;

// Built-in modules that don't need to be fetched from registry
// These are provided natively by the runtime (test comment)
const BUILTIN_MODULES = ['serialport', 'mqtt'];

async function pathExists(filePath) {
    try {
        await fsp.stat(filePath);
        return true;
    } catch (e) {
        return false;
    }
}

async function ensureDir(dirPath) {
    if (!dirPath || dirPath === '.') {
        return;
    }
    if (await pathExists(dirPath)) {
        return;
    }
    const parentDir = path.dirname(dirPath);
    if (parentDir && parentDir !== dirPath) {
        await ensureDir(parentDir);
    }
    if (!(await pathExists(dirPath))) {
        await fsp.mkdir(dirPath);
    }
}

async function rmrfPromises(dirPath) {
    if (!(await pathExists(dirPath))) {
        return;
    }
    let items;
    try {
        items = await fsp.readdir(dirPath);
    } catch (e) {
        return;
    }
    for (const item of items) {
        const itemPath = path.join(dirPath, item);
        try {
            const s = await fsp.stat(itemPath);
            if (s.isDirectory()) {
                await rmrfPromises(itemPath);
            } else {
                await fsp.unlink(itemPath);
            }
        } catch (e) {
        }
    }
    try {
        await fsp.rmdir(dirPath);
    } catch (e) {
    }
}

export async function install(args) {
    const options = parseArgs(args);
    const projectDir = options.dir || '.';
    const packageJsonPath = path.join(projectDir, 'package.json');
    const nodeModulesPath = path.join(projectDir, 'node_modules');

    let packageJson = { dependencies: {} };
    if (await pathExists(packageJsonPath)) {
        try {
            packageJson = JSON.parse(await fsp.readFile(packageJsonPath, 'utf-8'));
        } catch (e) {
            throw new Error(`Invalid package.json: ${e.message}`);
        }
    }
    if (!packageJson.dependencies || typeof packageJson.dependencies !== 'object') {
        packageJson.dependencies = {};
    }

    await ensureDir(nodeModulesPath);

    const packages = options.packages.length > 0 ? options.packages : Object.keys(packageJson.dependencies || {});

    if (packages.length === 0) {
        console.log('No packages to install.');
        console.log('Usage: tjs-npm install <package> [<package> ...]');
        return;
    }

    const resolved = new Map();
    const inflight = new Map();
    const ioLimit = createLimiter(options.concurrency || DEFAULT_CONCURRENCY);
    for (const pkg of packages) {
        await installPackage(pkg, packageJson, nodeModulesPath, options, resolved, inflight, ioLimit, true);
    }

    await fsp.writeFile(packageJsonPath, JSON.stringify(packageJson, null, 2) + '\n');
    console.log('Done.');
}

async function installPackage(pkgSpec, packageJson, nodeModulesPath, options, resolved, inflight, ioLimit, saveToManifest) {
    const { name, version } = parsePackageSpec(pkgSpec);

    // Skip download for built-in modules
    if (BUILTIN_MODULES.includes(name)) {
        const resolvedVersion = version || 'builtin';
        console.log(`Installing ${name}@${resolvedVersion}...`);
        console.log(`Installed ${name}@${resolvedVersion} (built-in)`);
        if (saveToManifest) {
            packageJson.dependencies[name] = version ? `^${version}` : 'builtin';
        }
        resolved.set(name, resolvedVersion);
        return;
    }

    console.log(`Installing ${name}@${version || 'latest'}...`);

    const registry = options.registry || DEFAULT_REGISTRY;
    const metadata = await withStage(`fetch metadata for ${name}`, () => fetchPackageMetadata(registry, name));

    let resolvedVersion = version || metadata['dist-tags'].latest;
    let tarballUrl = metadata.versions[resolvedVersion]?.dist?.tarball;

    if (!tarballUrl) {
        resolvedVersion = findSatisfyingVersion(metadata.versions, version || '*');
        if (resolvedVersion) {
            tarballUrl = metadata.versions[resolvedVersion].dist.tarball;
        }
    }

    if (!tarballUrl) {
        throw new Error(`Could not find version ${version || 'latest'} of ${name}`);
    }

    if (resolved.get(name) === resolvedVersion) {
        console.log(`Using cached ${name}@${resolvedVersion}`);
        return;
    }

    if (inflight.has(name)) {
        await inflight.get(name);
        return;
    }

    const installPromise = (async () => {
        const installDir = path.join(nodeModulesPath, name);
        await withStage(
            `download+extract ${name}@${resolvedVersion} into ${installDir}`,
            () => ioLimit(() => extractTarballFromUrl(tarballUrl, installDir))
        );

        const installedPackageJson = await withStage(`read installed package.json for ${name}@${resolvedVersion}`, () =>
            readInstalledPackageJson(installDir));
        if (saveToManifest) {
            packageJson.dependencies[name] = `^${resolvedVersion}`;
        }
        resolved.set(name, resolvedVersion);

        const dependencies = installedPackageJson.dependencies || {};
        const depsToInstall = Object.entries(dependencies).filter(([depName]) => !BUILTIN_MODULES.includes(depName));
        await Promise.all(depsToInstall.map(([depName, depVersion]) => {
            if (resolved.has(depName)) {
                return Promise.resolve();
            }
            return withStage(`install dependency ${depName}@${depVersion} for ${name}@${resolvedVersion}`, () =>
                installPackage(`${depName}@${depVersion}`, packageJson, nodeModulesPath, options, resolved, inflight, ioLimit, false));
        }));

        console.log(`Installed ${name}@${resolvedVersion}`);
    })();

    inflight.set(name, installPromise);
    try {
        await installPromise;
    } finally {
        if (inflight.get(name) === installPromise) {
            inflight.delete(name);
        }
    }
}

function createLimiter(maxConcurrent) {
    let active = 0;
    const queue = [];

    const schedule = () => {
        if (active >= maxConcurrent || queue.length === 0) {
            return;
        }
        const { fn, resolve, reject } = queue.shift();
        active++;
        Promise.resolve()
            .then(fn)
            .then(resolve, reject)
            .finally(() => {
                active--;
                schedule();
            });
    };

    return (fn) => new Promise((resolve, reject) => {
        queue.push({ fn, resolve, reject });
        schedule();
    });
}

function parsePackageSpec(spec) {
    if (spec.startsWith('@')) {
        const slashIndex = spec.indexOf('/');
        const versionIndex = spec.lastIndexOf('@');
        if (slashIndex > 0 && versionIndex > slashIndex) {
            return {
                name: spec.substring(0, versionIndex),
                version: spec.substring(versionIndex + 1) || null
            };
        }
        return { name: spec, version: null };
    }
    const atIndex = spec.lastIndexOf('@');
    if (atIndex > 0) {
        return {
            name: spec.substring(0, atIndex),
            version: spec.substring(atIndex + 1)
        };
    }
    return { name: spec, version: null };
}

function findSatisfyingVersion(versions, range) {
    const candidates = Object.keys(versions).sort((a, b) => compareVersions(b, a));
    for (const ver of candidates) {
        if (satisfiesRange(ver, range)) {
            return ver;
        }
    }
    return null;
}

function compareVersions(a, b) {
    const partsA = a.split('.').map(Number);
    const partsB = b.split('.').map(Number);
    for (let i = 0; i < Math.max(partsA.length, partsB.length); i++) {
        const partA = partsA[i] || 0;
        const partB = partsB[i] || 0;
        if (partA !== partB) return partA - partB;
    }
    return 0;
}

function satisfiesRange(version, range) {
    if (!range || range === '*' || range === 'latest') {
        return true;
    }

    const cleaned = range.trim();
    const comparators = cleaned.match(/(?:>=|<=|>|<)\s*\d+(?:\.\d+){0,2}/g);
    if (comparators && comparators.join(' ') === cleaned.replace(/\s+/g, ' ')) {
        return comparators.every(comparator => satisfiesComparator(version, comparator));
    }

    const comparatorMatch = cleaned.match(/^(>=|<=|>|<)\s*(\d+(?:\.\d+){0,2})$/);
    if (comparatorMatch) {
        return satisfiesComparator(version, cleaned);
    }

    if (cleaned.includes('||')) {
        return cleaned.split('||').some(part => satisfiesRange(version, part.trim()));
    }

    const hyphenMatch = cleaned.match(/^(\d+(?:\.\d+){0,2})\s+-\s+(\d+(?:\.\d+){0,2})$/);
    if (hyphenMatch) {
        return compareVersions(version, hyphenMatch[1]) >= 0 && compareVersions(version, hyphenMatch[2]) <= 0;
    }

    const whitespaceParts = cleaned.split(/\s+/).filter(Boolean);
    if (whitespaceParts.length > 1) {
        return whitespaceParts.every(part => satisfiesRange(version, part));
    }

    return satisfiesSimpleRange(version, cleaned);
}

function satisfiesComparator(version, comparator) {
    const comparatorMatch = comparator.trim().match(/^(>=|<=|>|<)\s*(\d+(?:\.\d+){0,2})$/);
    if (!comparatorMatch) {
        return false;
    }
    const op = comparatorMatch[1];
    const target = comparatorMatch[2];
    const cmp = compareVersions(version, target);
    if (op === '>') return cmp > 0;
    if (op === '>=') return cmp >= 0;
    if (op === '<') return cmp < 0;
    if (op === '<=') return cmp <= 0;
    return false;
}

function satisfiesSimpleRange(version, cleaned) {
    const comparatorMatch = cleaned.match(/^(>=|<=|>|<)\s*(\d+(?:\.\d+){0,2})$/);
    if (comparatorMatch) {
        return satisfiesComparator(version, cleaned);
    }
    if (/^\d+$/.test(cleaned)) {
        const major = Number(cleaned);
        return getVersionParts(version)[0] === major;
    }
    if (/^\d+\.\d+$/.test(cleaned)) {
        const [ major, minor ] = cleaned.split('.').map(Number);
        const [ vMajor, vMinor ] = getVersionParts(version);
        return vMajor === major && vMinor === minor;
    }
    if (/^[\dxX*]+(?:\.[\dxX*]+){0,2}$/.test(cleaned) && /[xX*]/.test(cleaned)) {
        const rangeParts = cleaned.split('.');
        const versionParts = getVersionParts(version);
        for (let i = 0; i < rangeParts.length; i++) {
            const part = rangeParts[i];
            if (part === 'x' || part === 'X' || part === '*') {
                return true;
            }
            if (versionParts[i] !== Number(part)) {
                return false;
            }
        }
        return true;
    }
    if (cleaned.startsWith('^')) {
        const base = cleaned.slice(1);
        return compareVersions(version, base) >= 0 && sameMajor(version, base);
    }
    if (cleaned.startsWith('~')) {
        const base = cleaned.slice(1);
        return compareVersions(version, base) >= 0 && sameMajor(version, base) && sameMinor(version, base);
    }
    return compareVersions(version, cleaned) === 0;
}

function sameMajor(a, b) {
    return getVersionParts(a)[0] === getVersionParts(b)[0];
}

function sameMinor(a, b) {
    const partsA = getVersionParts(a);
    const partsB = getVersionParts(b);
    return partsA[0] === partsB[0] && partsA[1] === partsB[1];
}

function getVersionParts(version) {
    return version
        .split('.')
        .map(part => parseInt(String(part).replace(/[^0-9].*$/, ''), 10) || 0);
}

async function fetchPackageMetadata(registry, name) {
    return new Promise((resolve, reject) => {
        const url = `${registry}/${encodeURIComponent(name).replace('%2F', '%2f')}`;
        https.get(url, { headers: { accept: 'application/json' } }, res => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try {
                    resolve(JSON.parse(data));
                } catch (e) {
                    reject(new Error(`Failed to fetch ${url}: ${e.message}`));
                }
            });
        }).on('error', reject);
    });
}

async function extractTarballFromUrl(url, destDir) {
    await rmrfPromises(destDir);
    await ensureDir(destDir);

    const gunzip = createGunzipStream();
    const tarExtractor = createTarExtractor(destDir);

    await new Promise((resolve, reject) => {
        let settled = false;
        let chain = Promise.resolve();

        const fail = (err) => {
            if (!settled) {
                settled = true;
                reject(err);
            }
        };

        https.get(url, res => {
            res.on('data', chunk => {
                chain = chain.then(async () => {
                    const output = gunzipStreamPush(gunzip, chunk);
                    await tarExtractor.push(output);
                });
                chain.catch(fail);
            });

            res.on('end', () => {
                chain = chain.then(async () => {
                    const finalOutput = gunzipStreamFinish(gunzip);
                    await tarExtractor.push(finalOutput);
                    await tarExtractor.finish();
                });
                chain.then(() => {
                    if (!settled) {
                        settled = true;
                        resolve();
                    }
                }).catch(fail);
            });

            res.on('error', fail);
        }).on('error', fail);
    });
}

function parseTarHeaderBlock(block) {
    let name = '';
    for (let i = 0; i < 100; i++) {
        const char = block[i];
        if (char === 0) break;
        name += String.fromCharCode(char);
    }
    name = name.replace(/\0.*$/, '').trim();
    if (!name) return null;

    const typeFlag = block[156];
    const type = typeFlag === 53 ? 'directory' : 'file';

    let sizeStr = '';
    for (let i = 0; i < 11; i++) {
        const char = block[124 + i];
        if (char === 0) break;
        sizeStr += String.fromCharCode(char);
    }
    const size = parseInt(sizeStr.trim(), 8) || 0;

    return { name, type, size };
}

function isZeroBlock(block) {
    for (let i = 0; i < 512; i++) {
        if (block[i] !== 0) {
            return false;
        }
    }
    return true;
}

function concatChunk(a, b) {
    if (!a || a.length === 0) return b;
    if (!b || b.length === 0) return a;
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
}

function createTarExtractor(destDir) {
    let pending = new Uint8Array(0);
    let ended = false;

    async function push(chunk) {
        if (ended || !chunk || chunk.length === 0) {
            return;
        }

        pending = concatChunk(pending, chunk);

        while (!ended) {
            if (pending.length < 512) {
                return;
            }

            const headerBlock = pending.subarray(0, 512);
            if (isZeroBlock(headerBlock)) {
                ended = true;
                return;
            }

            const header = parseTarHeaderBlock(headerBlock);
            if (!header) {
                ended = true;
                return;
            }

            const paddedSize = Math.ceil(header.size / 512) * 512;
            const totalSize = 512 + paddedSize;
            if (pending.length < totalSize) {
                return;
            }

            const relativeName = stripArchiveRoot(header.name);
            if (relativeName) {
                const entryPath = path.join(destDir, relativeName);
                if (header.type === 'directory') {
                    await ensureDir(entryPath);
                } else {
                    await ensureDir(path.dirname(entryPath));
                    const fileData = pending.subarray(512, 512 + header.size);
                    await fsp.writeFile(entryPath, Buffer.from(fileData));
                }
            }

            pending = pending.subarray(totalSize);
        }
    }

    async function finish() {
        if (!ended && pending.length > 0) {
            throw new Error('truncated tar stream');
        }
    }

    return { push, finish };
}

function stripArchiveRoot(entryName) {
    const normalized = entryName.replace(/^\/+/, '').replace(/\/+$/, '');
    if (!normalized) {
        return '';
    }

    const slashIndex = normalized.indexOf('/');
    if (slashIndex === -1) {
        return '';
    }
    return normalized.slice(slashIndex + 1);
}

async function readInstalledPackageJson(installDir) {
    const packageJsonPath = path.join(installDir, 'package.json');
    if (!(await pathExists(packageJsonPath))) {
        throw new Error(`Installed package is missing package.json: ${packageJsonPath}`);
    }
    return JSON.parse(await fsp.readFile(packageJsonPath, 'utf-8'));
}

async function withStage(stage, fn) {
    try {
        return await fn();
    } catch (e) {
        throw new Error(`${stage}: ${e.message || e}`);
    }
}

function parseArgs(args) {
    const options = { packages: [], dir: '.' };
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '-g' || args[i] === '--global') {
            throw new Error('Global install not supported');
        } else if (args[i] === '--registry' && args[i + 1]) {
            options.registry = args[++i];
        } else if ((args[i] === '--dir' || args[i] === '-C') && args[i + 1]) {
            options.dir = args[++i];
        } else if (args[i] === '--concurrency' && args[i + 1]) {
            const value = Number(args[++i]);
            if (Number.isFinite(value) && value > 0) {
                options.concurrency = Math.floor(value);
            }
        } else if (!args[i].startsWith('-')) {
            options.packages.push(args[i]);
        }
    }
    return options;
}
