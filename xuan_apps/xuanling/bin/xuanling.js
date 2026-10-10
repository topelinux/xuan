#!/usr/bin/env xuan

import { runCli } from '../src/cli/index.js';

function normalizeArgv(argv) {
  if (argv[1] === 'run') {
    const args = argv.slice(2);
    if (args[0] && args[0].endsWith('.js')) {
      return args.slice(1);
    }
    return args;
  }
  return argv.slice(2);
}

runCli(normalizeArgv(process.argv)).catch((err) => {
  const msg = err && err.message ? err.message : String(err);
  console.error(`[xuanling] fatal: ${msg}`);
  if (process.env.XUANLING_DEBUG === '1' && err && err.stack) {
    console.error(err.stack);
  }
  process.exit(1);
});
