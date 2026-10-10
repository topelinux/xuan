import { mkdir } from 'node:fs/promises';
import { createInterface } from 'node:readline';
import { createAgentRuntime } from '../agent/loop.js';

function parseArgs(argv) {
  const out = {
    trustAll: false,
    configPath: './config.json',
    workspacePath: '/tmp/xuanling-workspace',
    sessionKey: 'default',
    command: 'chat',
    askText: ''
  };

  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];

    if (token === '--trust-all') {
      out.trustAll = true;
      continue;
    }

    if (token === '--config' && argv[i + 1]) {
      out.configPath = argv[i + 1];
      i += 1;
      continue;
    }

    if (token === '--workspace' && argv[i + 1]) {
      out.workspacePath = argv[i + 1];
      i += 1;
      continue;
    }

    if (token === '--session' && argv[i + 1]) {
      out.sessionKey = argv[i + 1];
      i += 1;
      continue;
    }

    if (token === 'ask') {
      out.command = 'ask';
      out.askText = argv.slice(i + 1).join(' ').trim();
      break;
    }

    if (token === 'chat') {
      out.command = 'chat';
      continue;
    }

    if (token === 'help' || token === '--help' || token === '-h') {
      out.command = 'help';
      continue;
    }
  }

  return out;
}

function printHelp() {
  console.log(`xuanling - AI agent for xuan devices\n
Usage:
  xuanling chat [--config path] [--workspace path] [--session key] [--trust-all]
  xuanling ask "帮我安装 dayjs 并写 demo" [--trust-all]

Defaults:
  config:    ./config.json
  workspace: /tmp/xuanling-workspace
  session:   default

Notes:
  - 默认高风险动作需要确认
  - 使用 --trust-all 可跳过确认`);
}

function askLine(rl, prompt) {
  return new Promise((resolve) => rl.question(prompt, resolve));
}

export async function runCli(argv) {
  const args = parseArgs(argv);

  if (args.command === 'help') {
    printHelp();
    return;
  }

  await mkdir(args.workspacePath, { recursive: true }).catch(() => {});

  const rl = createInterface({ input: process.stdin, output: process.stdout });

  const runtime = await createAgentRuntime({
    configPath: args.configPath,
    workspacePath: args.workspacePath,
    sessionKey: args.sessionKey,
    trustAll: args.trustAll,
    approve: async (action, detail) => {
      const answer = await askLine(rl, `\n[确认] ${action}: ${detail}\n允许执行? [y/N] `);
      const v = String(answer || '').trim().toLowerCase();
      return v === 'y' || v === 'yes';
    }
  });

  console.log('玄灵已启动（对话模式）');
  console.log(`workspace: ${args.workspacePath}`);
  console.log(`session: ${args.sessionKey}`);
  if (args.trustAll) console.log('安全策略: trust-all 已开启');
  console.log('输入 exit 退出。\n');

  if (args.command === 'ask') {
    if (!args.askText) {
      console.error('ask 模式缺少问题文本');
      rl.close();
      process.exitCode = 2;
      return;
    }
    const out = await runtime.handleUserMessage(args.askText);
    console.log(out);
    rl.close();
    return;
  }

  while (true) {
    const input = await askLine(rl, '玄灵> ');
    const text = String(input || '').trim();

    if (!text) continue;
    if (text === 'exit' || text === 'quit') {
      rl.close();
      return;
    }

    try {
      const out = await runtime.handleUserMessage(text);
      console.log(`\n${out}\n`);
    } catch (err) {
      const msg = (err && err.message) ? err.message : String(err);
      console.error(`\n[error] ${msg}\n`);
    }
  }
}
