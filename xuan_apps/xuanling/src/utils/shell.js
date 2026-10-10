let spawnSync;

try {
  ({ spawnSync } = await import('node:child_process'));
} catch {
  spawnSync = null;
}

export function runCommand(command, args = [], options = {}) {
  if (!spawnSync) {
    return {
      ok: false,
      code: -1,
      stdout: '',
      stderr: 'child_process is not available'
    };
  }

  const result = spawnSync(command, args, {
    encoding: 'utf-8',
    timeout: options.timeout || 4000,
    ...options
  });

  return {
    ok: result.status === 0,
    code: result.status,
    stdout: result.stdout || '',
    stderr: result.stderr || ''
  };
}
