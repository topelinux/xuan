#!/usr/bin/env tjs
import { init } from '../src/init.js';
import { install } from '../src/install.js';
import { uninstall } from '../src/uninstall.js';

let args = tjs.args.slice(1);
if (args[0] === 'run') {
    args = args.slice(1);
}
const scriptPath = args[0];
const command = args[1];
const commandArgs = args.slice(2);

async function main() {
    switch (command) {
        case 'init':
            await init(commandArgs);
            break;
        case 'install':
        case 'i':
            await install(commandArgs);
            break;
        case 'uninstall':
        case 'remove':
        case 'rm':
            await uninstall(commandArgs);
            break;
        default:
            if (!command) {
                console.log('Usage: tjs-npm <command>\n');
                console.log('Commands:');
                console.log('  init          Create a package.json');
                console.log('  install       Install a package');
                console.log('  uninstall     Remove a package');
                console.log('\nOptions:');
                console.log('  -h, --help    Show help');
                process.exit(1);
            }
            console.error(`Unknown command: ${command}`);
            process.exit(1);
    }
}

main().catch(err => {
    if (err && err.message) {
        console.error('Error:', err.message);
    }
    if (err && err.stack) {
        console.error(err.stack);
    } else {
        console.error('Error:', err.message || err);
    }
    process.exit(1);
});
