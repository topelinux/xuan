import { readFile, mkdir } from 'node:fs/promises';
import { dirname, isAbsolute, resolve } from 'node:path';

const defaultConfig = {
  llm: {
    provider: 'openai',
    model: 'qwen3.5-plus',
    apiKey: process.env.LLM_API_KEY || '',
    baseUrl: process.env.LLM_BASE_URL || 'https://coding.dashscope.aliyuncs.com/v1'
  },
  memory: {
    path: '/tmp/xuanling-workspace/memory'
  },
  agent: {
    maxToolIterations: 10,
    tokenBudget: 8000
  },
  system: [
    '你是玄灵，一个运行在设备本机上的 xuan 对话助手。',
    '目标：通过对话帮助用户完成设备诊断、日志收集、xuan npm 安装与示例验证。',
    '默认平台：OpenWrt 类嵌入式设备。',
    '要求：动作明确、输出可执行命令、先验证再总结。',
    '当涉及安装、写文件、执行脚本等变更动作时，应先解释将做什么。'
  ].join('\n')
};

let activeConfig = { ...defaultConfig };

export async function loadConfig(path = './config.json') {
  const configFile = resolve(path);
  const configDir = dirname(configFile);

  try {
    const raw = await readFile(configFile, 'utf-8');
    const parsed = JSON.parse(raw);
    const parsedMemory = { ...(parsed.memory || {}) };

    if (parsedMemory.path && !isAbsolute(parsedMemory.path)) {
      parsedMemory.path = resolve(configDir, parsedMemory.path);
    }

    activeConfig = {
      ...defaultConfig,
      ...parsed,
      llm: { ...defaultConfig.llm, ...(parsed.llm || {}) },
      memory: { ...defaultConfig.memory, ...parsedMemory },
      agent: { ...defaultConfig.agent, ...(parsed.agent || {}) }
    };
  } catch {
    activeConfig = { ...defaultConfig };
  }

  await mkdir(activeConfig.memory.path, { recursive: true }).catch(() => {});
  return activeConfig;
}

export function getConfig() {
  return activeConfig;
}
