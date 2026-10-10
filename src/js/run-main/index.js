/* global tjs */

import getopts from 'tjs:getopts';
import path from 'tjs:path';
import { WASI } from 'tjs:wasi';

import { evalStdin } from './eval-stdin.js';
import { mkdirSync } from './mkdirSync.js';
import applyNodeBootstrap from './node-bootstrap.js';
import { runTests } from './run-tests.js';

const core = globalThis[Symbol.for('tjs.internal.core')];
const utf8Decoder = new TextDecoder();
const utf8Encoder = new TextEncoder();

/**
 * Before we do anything else, create our "home" directory,
 * so other parts of the code which need it can find it.
 * Also set the cookie jar path, since it's needed when
 * initializing lws.
 */
const TJS_HOME = tjs.env.TJS_HOME ?? path.join(tjs.homeDir, '.tjs');

try {
    mkdirSync(TJS_HOME, { recursive: true });
} catch (_) {
    // Ignore.
}

core.setCookieJarPath(path.join(TJS_HOME, 'cookies.txt'));

/**
 * Trailer for standalone binaries. When some code gets bundled with the tjs
 * executable we add a 12 byte trailer. The first 8 bytes are the magic
 * string that helps us understand this is a standalone binary, and the
 * remaining 4 are the offset (from the beginning of the binary) where the
 * bundled data is located.
 *
 * The offset is stored as a 32bit little-endian number.
 */
const Trailer = {
    Magic: 'tx1k1.js',
    MagicSize: 8,
    DataSize: 4,
    Size: 12
};

const exeName = path.basename(tjs.args[0]);
const help = `Usage: ${exeName} [options] [subcommand]

Options:
  -v, --version
        Print version information

  -h, --help
        Print help

  -e, --eval EXPRESSION
        Evaluate a JavaScript expression (Node.js style)

  --bootstrap PATH|node|none
        Load a bootstrap module before running user code

  --no-bootstrap
        Disable bootstrap loading

  --memory-limit LIMIT
        Set the memory limit for the JavaScript runtime

  --stack-size STACKSIZE
        Set the maximum JavaScript stack size

Subcommands:
  run
        Run a JavaScript program

  npm
        Run built-in npm-compatible package manager

  eval
        Evaluate a JavaScript expression

  test
        Run tests in the given directory

  compile infile [outfile]
        Compile the given file into a standalone executable`;

const helpEval = `Usage: ${exeName} eval EXPRESSION`;

const helpRun = `Usage: ${exeName} run FILE`;

// First, let's check if this is a standalone binary.
const handledStandalone = await (async () => {
    const exef = await tjs.open(tjs.exePath, 'rb');
    const exeSize = (await exef.stat()).size;
    const trailerBuf = new Uint8Array(Trailer.Size);

    await exef.read(trailerBuf, exeSize - Trailer.Size);

    const magic = new Uint8Array(trailerBuf.buffer, 0, Trailer.MagicSize);
    const maybeMagic = new TextDecoder().decode(magic);

    if (maybeMagic === Trailer.Magic) {
        const dw = new DataView(trailerBuf.buffer, Trailer.MagicSize, Trailer.DataSize);
        const offset = dw.getUint32(0, true);
        const buf = new Uint8Array(exeSize - offset - Trailer.Size);

        await exef.read(buf, offset);
        await exef.close();

        return buf;
    }

    await exef.close();
    return null;
})();

if (handledStandalone) {
    try {
        await applyBootstrap('node');
        const bytecode = tjs.engine.deserialize(handledStandalone);

        await tjs.engine.evalBytecode(bytecode);
    } catch (error) {
        handleMainError(error);
    }
} else {
    await main().catch(handleMainError);
}

async function main() {
const options = getopts(tjs.args.slice(1), {
    alias: {
        eval: 'e',
        help: 'h',
        version: 'v'
    },
    boolean: [ 'h', 'v', 'no-bootstrap' ],
    string: [ 'e', 'bootstrap' ],
    stopEarly: true,
    unknown: option => {
        if (![ 'memory-limit', 'stack-size', 'bootstrap', 'no-bootstrap' ].includes(option)) {
            throw `unrecognized option: ${option}`;
        }

        return option;
    }
});

if (options.help) {
    console.log(help);
} else if (options.version) {
    console.log(`v${tjs.version}`);
} else {
    const memoryLimit = options['memory-limit'];
    const stackSize = options['stack-size'];
    const bootstrapOpt = options.bootstrap ?? tjs.env.TJS_BOOTSTRAP ?? 'node';
    const disableBootstrap = options['no-bootstrap'] === true || bootstrapOpt === 'none';

    if (typeof memoryLimit !== 'undefined') {
        core.setMemoryLimit(parseNumberOption(memoryLimit, 'memory-limit'));
    }

    if (typeof stackSize !== 'undefined') {
        core.setMaxStackSize(parseNumberOption(stackSize, 'stack-size'));
    }

    const evalExpr = options.eval;
    const argvRest = tjs.args.slice(1);
    const hasEvalFlag = argvRest.includes('-e') || argvRest.includes('--eval');
    const hasEvalOption = hasEvalFlag && typeof evalExpr === 'string';
    const knownCommands = new Set([ 'run', 'npm', 'eval', 'test', 'compile' ]);
    let [ command, ...subargv ] = options._;

    if (hasEvalOption) {
        command = 'eval';
        subargv = [ evalExpr, ...subargv ];
    }

    if (command && !knownCommands.has(command)) {
        subargv = [ command, ...subargv ];
        command = 'run';
    }

    const shouldBootstrap = !disableBootstrap && [ 'run', 'eval', 'test' ].includes(command ?? '');

    if (shouldBootstrap) {
        await applyBootstrap(bootstrapOpt);
    }

    if (!command) {
        if (tjs.stdin.isTerminal) {
            core.runRepl();
        } else {
            evalStdin();
        }
    } else if (command === 'eval') {
        const [ expr ] = subargv;

        if (typeof expr === 'undefined') {
            throw helpEval;
        }

        core.evalScript(expr);
    } else if (command === 'run') {
        const [ filename ] = subargv;

        if (!filename) {
            throw helpRun;
        }

        const ext = path.extname(filename).toLowerCase();

        if (ext === '.wasm') {
            try {
                await import('wasm');
            } catch (e) {
                if (typeof core.wasm === 'undefined') {
                    throw e;
                }
            }
            const bytes = await tjs.readFile(filename);
            const module = new WebAssembly.Module(bytes);
            const wasi = new WASI({
                version: 'wasi_snapshot_preview1',
                args: subargv,
                preopens: {
                    '.': tjs.cwd,
                    '/': '/'
                }
            });
            const instance = new WebAssembly.Instance(module, wasi.getImportObject());

            wasi.start(instance);
        } else {
            await runScriptEntry(filename);
        }
    } else if (command === 'npm') {
        const npmMod = await import('tjs:npm');
        if (typeof npmMod.run !== 'function') {
            throw 'Built-in npm module is missing run()';
        }
        await npmMod.run(subargv);
    } else if (command === 'test') {
        const [ dir ] = subargv;

        await runTests(dir);
    } else if (command === 'compile') {
        const [ infile, outfile ] = subargv;

        if (!infile) {
            throw help;
        }

        const resolvedInfile = path.resolve(infile);
        const infilePath = path.parse(infile);
        const data = await tjs.readFile(infile);
        const source = await shouldRunAsCjs(resolvedInfile)
            ? utf8Encoder.encode(wrapCompiledCjsEntry(utf8Decoder.decode(data)))
            : data;
        const bytecode = tjs.engine.serialize(tjs.engine.compile(source, infilePath.base));
        const exe = await tjs.readFile(tjs.exePath);
        const exeSize = exe.length;
        const newBuffer = exe.buffer.transfer(exeSize + bytecode.length + Trailer.Size);
        const newExe = new Uint8Array(newBuffer);

        newExe.set(bytecode, exeSize);
        newExe.set(new TextEncoder().encode(Trailer.Magic), exeSize + bytecode.length);

        const dw = new DataView(newBuffer, exeSize + bytecode.length + Trailer.MagicSize, Trailer.DataSize);

        dw.setUint32(0, exeSize, true);

        let newFileName = outfile ?? `${infilePath.name}`;

        if (navigator.userAgentData.platform === 'Windows' && !newFileName.endsWith('.exe')) {
            newFileName += '.exe';
        }

        const newFile = await tjs.open(newFileName, 'w', 0o755);

        await newFile.write(newExe);
        await newFile.close();
    } else {
        throw help;
    }
}
}

function handleMainError(error) {
    if (error && typeof error === 'object') {
        const message = typeof error.message === 'string' ? error.message : null;
        const stack = typeof error.stack === 'string' ? error.stack : null;
        const code = typeof error.code === 'string' ? error.code : null;
        const requireStack = Array.isArray(error.requireStack) ? error.requireStack : null;

        if (stack || message) {
            let output = stack ?? '';
            if (message && (!stack || !stack.includes(message))) {
                output = stack ? `Error: ${message}\n${stack}` : `Error: ${message}`;
            }
            const hasRequireStack = output.includes('Require stack:');
            if (code === 'MODULE_NOT_FOUND' && requireStack && requireStack.length && !hasRequireStack) {
                output += `\nRequire stack:\n${requireStack.map(entry => `- ${entry}`).join('\n')}`;
            }
            console.error(output);
            tjs.exit(1);
            return;
        }
    }

    if (error && error.message) {
        console.error(error.message);
    } else {
        console.error(String(error));
    }
    tjs.exit(1);
}

async function applyBootstrap(bootstrapOpt) {
    if (!bootstrapOpt || bootstrapOpt === 'node') {
        await applyNodeBootstrap();
        return;
    }

    if (bootstrapOpt === 'none') {
        return;
    }

    const modulePath = path.resolve(bootstrapOpt);
    await import(modulePath);
}


function parseNumberOption(num, option) {
    const n = Number.parseInt(num, 10);

    if (Number.isNaN(n)) {
        throw new Error(`Invalid number ${num} for option ${option}`);
    }

    return n;
}

function wrapCompiledCjsEntry(source) {
    return `{
const __tjsPathModule = globalThis[Symbol.for('tjs.internal.modules.path')];
const __filename = tjs.exePath;
const __dirname = __tjsPathModule.dirname(__filename);
const module = {
    id: '.',
    filename: __filename,
    path: __dirname,
    exports: {},
    loaded: false,
    parent: null,
    children: [],
    require: globalThis.require
};
const exports = module.exports;
try {
    ((exports, require, module, __filename, __dirname) => {
${source}
    })(exports, globalThis.require, module, __filename, __dirname);
} finally {
    module.loaded = true;
}
}`;
}

async function runScriptEntry(filename) {
    const resolved = path.resolve(filename);

    if (await shouldRunAsCjs(resolved)) {
        runAsCjs(resolved);
        return;
    }

    await core.evalFile(filename);
}

function runAsCjs(filename) {
    if (typeof globalThis.require !== 'function') {
        throw new Error('CommonJS loader is not available');
    }

    globalThis.require(filename);
}

async function shouldRunAsCjs(filename) {
    const ext = path.extname(filename).toLowerCase();

    if (ext === '.cjs') {
        return true;
    }

    if (ext === '.mjs') {
        return false;
    }

    if (ext !== '.js') {
        return false;
    }

    const pkgType = await findNearestPackageType(path.dirname(filename));
    if (pkgType === 'module') {
        return false;
    }

    if (pkgType === 'commonjs') {
        return true;
    }

    return !(await fileLooksLikeEsm(filename));
}

async function findNearestPackageType(startDir) {
    let dir = startDir;

    while (true) {
        const pkgPath = path.join(dir, 'package.json');
        const result = await readPackageType(pkgPath);

        if (result.found) {
            return result.type;
        }

        const parent = path.dirname(dir);
        if (parent === dir) {
            return null;
        }
        dir = parent;
    }
}

async function readPackageType(pkgPath) {
    try {
        const raw = await tjs.readFile(pkgPath);
        const pkg = JSON.parse(utf8Decoder.decode(raw));

        if (typeof pkg?.type === 'string') {
            return { found: true, type: pkg.type };
        }

        // The nearest package.json establishes the package boundary.
        // Without an explicit "type", Node defaults .js to CommonJS.
        return { found: true, type: null };
    } catch (_) {
        // Ignore read / parse failures and continue searching upward.
    }

    return { found: false, type: null };
}

async function fileLooksLikeEsm(filename) {
    try {
        const source = utf8Decoder.decode(await tjs.readFile(filename));
        return source.includes('import ') || source.includes('export ');
    } catch (_) {
        return false;
    }
}
