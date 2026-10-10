import { init } from '../../../tools/npm/src/init.js';
import { install } from '../../../tools/npm/src/install.js';
import { uninstall } from '../../../tools/npm/src/uninstall.js';

function normalizeArgv(argvLike = []) {
    let args = Array.from(argvLike);
    if (args[0] === 'run') {
        args = args.slice(1);
    }
    if (args[0] && typeof args[0] === 'string') {
        const basename = args[0].split(/[\\/]/).pop();
        if (basename === 'tjs-npm.js' || basename === 'tjs-npm') {
            args = args.slice(1);
        }
    }
    return args;
}

export async function run(argv = tjs.args.slice(1)) {
    const args = normalizeArgv(argv);
    const command = args[0];
    const commandArgs = args.slice(1);

    switch (command) {
        case 'init':
            await init(commandArgs);
            return;
        case 'install':
        case 'i':
            await install(commandArgs);
            return;
        case 'uninstall':
        case 'remove':
        case 'rm':
            await uninstall(commandArgs);
            return;
        default:
            if (!command || command === '-h' || command === '--help') {
                console.log('Usage: tjs npm <command>\n');
                console.log('Commands:');
                console.log('  init          Create a package.json');
                console.log('  install       Install a package');
                console.log('  uninstall     Remove a package');
                return;
            }
            throw new Error(`Unknown npm command: ${command}`);
    }
}

export default {
    run,
};
