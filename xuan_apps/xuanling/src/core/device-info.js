import os from 'node:os';
import { readFile } from 'node:fs/promises';
import { runCommand } from '../utils/shell.js';

async function safeRead(path) {
  try {
    return await readFile(path, 'utf-8');
  } catch {
    return '';
  }
}

function parseMemInfo(raw) {
  const out = {};
  for (const line of raw.split('\n')) {
    const m = line.match(/^([A-Za-z_\(\)]+):\s+(\d+)\s+kB$/);
    if (m) out[m[1]] = Number(m[2]) * 1024;
  }
  return out;
}

function safeCall(fn, fallback) {
  try {
    if (typeof fn === 'function') {
      return fn();
    }
  } catch {
    // fallback
  }
  return fallback;
}

export async function collectDeviceInfo() {
  const osRelease = await safeRead('/etc/os-release');
  const openwrtRelease = await safeRead('/etc/openwrt_release');
  const memInfo = parseMemInfo(await safeRead('/proc/meminfo'));

  const loadavgRaw = await safeRead('/proc/loadavg');
  const loadavg = loadavgRaw
    ? loadavgRaw.trim().split(/\s+/).slice(0, 3).map(Number)
    : safeCall(() => os.loadavg(), [0, 0, 0]);

  const netIfs = safeCall(() => os.networkInterfaces(), {});
  const networkNames = Object.keys(netIfs || {});

  const hasLogread = runCommand('which', ['logread']).ok;
  const hasDmesg = runCommand('which', ['dmesg']).ok;

  const uptimeRaw = safeCall(() => process.uptime(), 0);
  const uptimeSec = Number.isFinite(uptimeRaw) ? Math.floor(uptimeRaw) : 0;

  return {
    runtime: {
      node: process.version,
      platform: process.platform,
      arch: process.arch,
      uptimeSec
    },
    system: {
      hostname: safeCall(() => os.hostname(), 'unknown'),
      release: safeCall(() => os.release(), 'unknown'),
      cpus: safeCall(() => os.cpus().length, 1),
      loadavg,
      totalMem: memInfo.MemTotal || safeCall(() => os.totalmem(), 0),
      freeMem: memInfo.MemAvailable || memInfo.MemFree || safeCall(() => os.freemem(), 0),
      networkInterfaces: networkNames
    },
    distro: {
      isOpenWrt: openwrtRelease.includes('OpenWrt') || osRelease.includes('OpenWrt'),
      osRelease: osRelease.trim(),
      openwrtRelease: openwrtRelease.trim()
    },
    logs: {
      hasLogread,
      hasDmesg,
      hasMessagesFile: Boolean(await safeRead('/var/log/messages')),
      hasSyslogFile: Boolean(await safeRead('/var/log/syslog'))
    }
  };
}

export function toMemorySummary(totalMem, freeMem) {
  const used = Math.max(totalMem - freeMem, 0);
  const usagePercent = totalMem > 0 ? (used / totalMem) * 100 : 0;
  return { used, usagePercent };
}

export async function collectLogSource(source, lines) {
  const n = String(lines || 120);
  const choices = source === 'auto' ? ['logread', 'dmesg', 'messages', 'syslog'] : [source];

  for (const item of choices) {
    if (item === 'logread') {
      const r = runCommand('logread', ['-l', n]);
      if (r.ok && r.stdout.trim()) return { source: 'logread', content: r.stdout.trim() };
    }

    if (item === 'dmesg') {
      const r = runCommand('dmesg', []);
      if (r.ok && r.stdout.trim()) {
        const sliced = r.stdout.trim().split('\n').slice(-Number(n)).join('\n');
        return { source: 'dmesg', content: sliced };
      }
    }

    if (item === 'messages') {
      const raw = await safeRead('/var/log/messages');
      if (raw.trim()) return { source: '/var/log/messages', content: raw.trim().split('\n').slice(-Number(n)).join('\n') };
    }

    if (item === 'syslog') {
      const raw = await safeRead('/var/log/syslog');
      if (raw.trim()) return { source: '/var/log/syslog', content: raw.trim().split('\n').slice(-Number(n)).join('\n') };
    }
  }

  return { source: 'none', content: '' };
}
