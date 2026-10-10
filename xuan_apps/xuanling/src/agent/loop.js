import { loadConfig, getConfig } from './config.js';
import { callLLM } from './llm.js';
import { createTools } from './tools.js';
import { SessionStore } from './session-store.js';

function toToolMessage(toolCallId, payload) {
  return {
    role: 'tool',
    tool_call_id: toolCallId,
    content: JSON.stringify(payload, null, 2).slice(0, 12000)
  };
}

export async function createAgentRuntime(options) {
  const cfg = await loadConfig(options.configPath);

  const store = new SessionStore(cfg.memory.path);
  await store.initialize();
  const sessionMessages = await store.load(options.sessionKey);

  const context = {
    workspacePath: options.workspacePath,
    trustAll: options.trustAll,
    approve: options.approve
  };

  const tools = createTools(context);

  async function handleUserMessage(input) {
    const messages = [
      { role: 'system', content: cfg.system },
      ...sessionMessages,
      { role: 'user', content: input }
    ];

    const maxIter = Number(cfg.agent.maxToolIterations || 10);

    for (let i = 0; i < maxIter; i += 1) {
      const reply = await callLLM(messages, tools);

      if (Array.isArray(reply.tool_calls) && reply.tool_calls.length > 0) {
        messages.push(reply);

        for (const call of reply.tool_calls) {
          const name = call.function.name;
          const tool = tools[name];
          if (!tool) {
            messages.push(toToolMessage(call.id, { success: false, error: `unknown tool: ${name}` }));
            continue;
          }

          let args = {};
          try {
            args = JSON.parse(call.function.arguments || '{}');
          } catch {
            args = {};
          }

          let result;
          try {
            result = await tool.execute(args);
          } catch (err) {
            result = { success: false, error: (err && err.message) ? err.message : String(err) };
          }

          messages.push(toToolMessage(call.id, result));
        }

        continue;
      }

      const text = reply.content || '';
      await store.append(options.sessionKey, 'user', input);
      await store.append(options.sessionKey, 'assistant', text);
      return text;
    }

    const fallback = '达到最大工具迭代次数，未完成任务。';
    await store.append(options.sessionKey, 'user', input);
    await store.append(options.sessionKey, 'assistant', fallback);
    return fallback;
  }

  return {
    config: getConfig(),
    handleUserMessage
  };
}
