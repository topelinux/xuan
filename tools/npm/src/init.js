import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'tjs:fs';
import path from 'tjs:path';

const DEFAULT_PACKAGE = {
    name: 'my-project',
    version: '1.0.0',
    description: '',
    main: 'index.js',
    scripts: {
        test: 'echo "Error: no test specified" && exit 1'
    },
    keywords: [],
    author: '',
    license: 'ISC'
};

export async function init(args) {
    const options = parseArgs(args);
    const projectDir = options.dir || '.';
    const packagePath = path.join(projectDir, 'package.json');

    let packageJson;

    if (existsSync(packagePath)) {
        const content = readFileSync(packagePath, 'utf-8');
        try {
            packageJson = JSON.parse(content);
            console.log(`Package already exists: ${packagePath}`);
            console.log('Use npm install to add dependencies.');
            return;
        } catch (e) {
            throw new Error(`Invalid package.json: ${e.message}`);
        }
    }

    const name = options.name || path.basename(process.cwd());
    packageJson = { ...DEFAULT_PACKAGE, name };

    if (options.yes || options.y) {
        ensureDir(projectDir);
        writeFileSync(packagePath, JSON.stringify(packageJson, null, 2) + '\n');
        console.log(`Created package.json: ${packagePath}`);
    } else {
        console.log('About to initialize a new package:');
        console.log(JSON.stringify(packageJson, null, 2));
        console.log('\nIs this OK? (yes) ');
        const answer = await prompt();
        if (answer.trim().toLowerCase() !== 'no') {
            ensureDir(projectDir);
            writeFileSync(packagePath, JSON.stringify(packageJson, null, 2) + '\n');
            console.log(`Created package.json: ${packagePath}`);
        } else {
            console.log('Aborted.');
        }
    }
}

function parseArgs(args) {
    const options = { dir: '.' };
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '-y' || args[i] === '--yes') {
            options.yes = true;
        } else if (args[i] === '--name' && args[i + 1]) {
            options.name = args[++i];
        } else if (!args[i].startsWith('-')) {
            options.dir = args[i];
        }
    }
    return options;
}

async function prompt() {
    return new Promise(resolve => {
        const chunks = [];
        process.stdin.on('data', chunk => chunks.push(chunk));
        process.stdin.on('end', () => resolve(chunks.join('')));
    });
}

function ensureDir(dirPath) {
    if (!dirPath || dirPath === '.' || existsSync(dirPath)) {
        return;
    }
    const parentDir = path.dirname(dirPath);
    if (parentDir && parentDir !== dirPath) {
        ensureDir(parentDir);
    }
    if (!existsSync(dirPath)) {
        mkdirSync(dirPath);
    }
}
