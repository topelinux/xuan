import { mkdir, readFile, writeFile, readdir } from 'node:fs/promises';
import { resolve, dirname, sep } from 'node:path';
import { collectDeviceInfo, collectLogSource, toMemorySummary } from '../core/device-info.js';
import { runCommand } from '../utils/shell.js';

function ensureInsideWorkspace(workspacePath, inputPath) {
  const base = resolve(workspacePath);
  const target = resolve(base, inputPath || '.');
  if (!(target === base || target.startsWith(base + sep))) {
    throw new Error('path is outside workspace');
  }
  return target;
}

function parseLines(v, fallback = 120) {
  const n = Number(v);
  if (!Number.isFinite(n) || n <= 0) return fallback;
  return Math.floor(n);
}

async function maybeApprove(ctx, action, detail) {
  if (ctx.trustAll) return true;
  if (!ctx.approve) return false;
  return Boolean(await ctx.approve(action, detail));
}

export function createTools(ctx) {
  return {
    doctor_openwrt: {
      description: '诊断设备环境，检查 OpenWrt、xuan、opkg、ubus、logread 等能力',
      parameters: {},
      execute: async () => {
        const info = await collectDeviceInfo();
        const checks = {
          xuan: runCommand('which', ['xuan']).ok,
          opkg: runCommand('which', ['opkg']).ok,
          ubus: runCommand('which', ['ubus']).ok,
          logread: runCommand('which', ['logread']).ok,
          dmesg: runCommand('which', ['dmesg']).ok
        };

        return { success: true, info, checks };
      }
    },

    collect_metrics: {
      description: '收集设备指标：CPU、内存、load、network',
      parameters: {},
      execute: async () => {
        const info = await collectDeviceInfo();
        const mem = toMemorySummary(info.system.totalMem, info.system.freeMem);
        return {
          success: true,
          metrics: {
            uptimeSec: info.runtime.uptimeSec,
            cpus: info.system.cpus,
            loadavg: info.system.loadavg,
            memory: {
              total: info.system.totalMem,
              free: info.system.freeMem,
              used: mem.used,
              usagePercent: Number(mem.usagePercent.toFixed(2))
            },
            networkInterfaces: info.system.networkInterfaces
          }
        };
      }
    },

    collect_logs: {
      description: '收集设备日志，支持 source=auto|logread|dmesg|messages|syslog 与 lines 参数',
      parameters: { source: 'string', lines: 'string' },
      execute: async (args) => {
        const source = args.source || 'auto';
        const lines = parseLines(args.lines, 120);
        const result = await collectLogSource(source, lines);
        return { success: true, source: result.source, lines, content: result.content };
      }
    },

    list_workspace_files: {
      description: '列出 workspace 下文件，path 为空则列出根目录',
      parameters: { path: 'string' },
      execute: async (args) => {
        const rel = args.path || '.';
        const abs = ensureInsideWorkspace(ctx.workspacePath, rel);
        const items = await readdir(abs).catch(() => []);
        return { success: true, path: rel, items };
      }
    },

    read_workspace_file: {
      description: '读取 workspace 下文件内容',
      parameters: { path: 'string' },
      execute: async (args) => {
        const abs = ensureInsideWorkspace(ctx.workspacePath, args.path);
        const content = await readFile(abs, 'utf-8');
        return { success: true, path: args.path, content: content.slice(0, 30000) };
      }
    },

    write_workspace_file: {
      description: '写入 workspace 文件（会覆盖）',
      parameters: { path: 'string', content: 'string' },
      execute: async (args) => {
        const ok = await maybeApprove(ctx, 'write_file', `write ${args.path}`);
        if (!ok) return { success: false, error: 'user denied write action' };

        const abs = ensureInsideWorkspace(ctx.workspacePath, args.path);
        await mkdir(dirname(abs), { recursive: true });
        await writeFile(abs, args.content || '', 'utf-8');
        return { success: true, path: args.path };
      }
    },

    run_shell: {
      description: '执行设备 shell 命令，默认工作目录为 workspace。谨慎使用。',
      parameters: { command: 'string' },
      execute: async (args) => {
        const command = args.command || '';
        const ok = await maybeApprove(ctx, 'run_shell', command);
        if (!ok) return { success: false, error: 'user denied shell action' };

        const r = runCommand('sh', ['-lc', command], {
          cwd: ctx.workspacePath,
          timeout: 120000
        });

        return {
          success: r.ok,
          code: r.code,
          stdout: (r.stdout || '').slice(0, 30000),
          stderr: (r.stderr || '').slice(0, 12000)
        };
      }
    },

    xuan_npm_install: {
      description: '在 workspace 下执行 xuan npm install <package>，用于安装 JS 包',
      parameters: { package: 'string' },
      execute: async (args) => {
        const pkg = (args.package || '').trim();
        if (!pkg) return { success: false, error: 'missing package name' };

        const ok = await maybeApprove(ctx, 'install_package', `xuan npm install ${pkg}`);
        if (!ok) return { success: false, error: 'user denied install action' };

        const r = runCommand('sh', ['-lc', `xuan npm install ${pkg}`], {
          cwd: ctx.workspacePath,
          timeout: 180000
        });

        return {
          success: r.ok,
          command: `xuan npm install ${pkg}`,
          stdout: (r.stdout || '').slice(0, 30000),
          stderr: (r.stderr || '').slice(0, 12000)
        };
      }
    },

    run_xuan_script: {
      description: '运行 workspace 中的 JS 文件，例如 index_demo.js',
      parameters: { entry: 'string' },
      execute: async (args) => {
        const entry = args.entry || 'index_demo.js';
        const abs = ensureInsideWorkspace(ctx.workspacePath, entry);
        const ok = await maybeApprove(ctx, 'run_script', `xuan ${entry}`);
        if (!ok) return { success: false, error: 'user denied script execution' };

        const r = runCommand('sh', ['-lc', `xuan ${JSON.stringify(abs)}`], {
          cwd: ctx.workspacePath,
          timeout: 120000
        });

        return {
          success: r.ok,
          entry,
          stdout: (r.stdout || '').slice(0, 30000),
          stderr: (r.stderr || '').slice(0, 12000)
        };
      }
    }
  };
}
