import { readFileSync, writeFileSync, existsSync, readdir, stat, unlinkSync, rmdirSync } from 'tjs:fs';
import path from 'tjs:path';

export async function uninstall(args) {
    const options = parseArgs(args);
    const packageJsonPath = path.join(options.dir || '.', 'package.json');
    const nodeModulesPath = path.join(options.dir || '.', 'node_modules');

    if (!existsSync(packageJsonPath)) {
        throw new Error('No package.json found');
    }

    let packageJson;
    try {
        packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8'));
    } catch (e) {
        throw new Error(`Invalid package.json: ${e.message}`);
    }

    if (options.packages.length === 0) {
        console.log('Usage: tjs-npm uninstall <package> [<package> ...]');
        return;
    }

    let changed = false;
    for (const pkg of options.packages) {
        const packagePath = path.join(nodeModulesPath, pkg);

        if (existsSync(packagePath)) {
            await rmrfPromises(packagePath);
            pruneEmptyScopeDir(nodeModulesPath, pkg);
            console.log(`Removed ${pkg}`);

            if (packageJson.dependencies?.[pkg]) {
                delete packageJson.dependencies[pkg];
                changed = true;
            }
        } else {
            console.log(`${pkg} is not installed`);
        }
    }

    if (changed) {
        writeFileSync(packageJsonPath, JSON.stringify(packageJson, null, 2) + '\n');
    }

    console.log('Done.');
}

function readdirProm(dirPath) {
    return new Promise((resolve, reject) => {
        readdir(dirPath, (err, list) => {
            if (err) reject(err);
            else resolve(list);
        });
    });
}

function statProm(filePath) {
    return new Promise((resolve, reject) => {
        stat(filePath, (err, s) => {
            if (err) reject(err);
            else resolve(s);
        });
    });
}

async function rmrfPromises(dirPath) {
    if (!existsSync(dirPath)) {
        return;
    }

    const items = await readdirProm(dirPath);
    for (const item of items) {
        const itemPath = path.join(dirPath, item);
        const s = await statProm(itemPath);
        if (s.isDirectory()) {
            await rmrfPromises(itemPath);
        } else {
            unlinkSync(itemPath);
        }
    }
    rmdirSync(dirPath);
}

function pruneEmptyScopeDir(nodeModulesPath, pkg) {
    if (!pkg.startsWith('@')) {
        return;
    }

    const slashIndex = pkg.indexOf('/');
    if (slashIndex === -1) {
        return;
    }

    const scopeDir = path.join(nodeModulesPath, pkg.slice(0, slashIndex));
    if (!existsSync(scopeDir)) {
        return;
    }

    readdir(scopeDir, (err, list) => {
        if (!err && list.length === 0) {
            try {
                rmdirSync(scopeDir);
            } catch (e) {
            }
        }
    });
}

function parseArgs(args) {
    const options = { packages: [], dir: '.' };
    for (let i = 0; i < args.length; i++) {
        if ((args[i] === '--dir' || args[i] === '-C') && args[i + 1]) {
            options.dir = args[++i];
        } else if (!args[i].startsWith('-')) {
            options.packages.push(args[i]);
        }
    }
    return options;
}
