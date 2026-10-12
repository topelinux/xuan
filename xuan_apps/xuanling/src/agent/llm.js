import { getConfig } from './config.js';

const VERSION = '0.1';

export function buildToolDefinitions(tools) {
  return Object.entries(tools).map(([name, tool]) => ({
    type: 'function',
    function: {
      name,
      description: tool.description,
      parameters: {
        type: 'object',
        properties: Object.fromEntries(
          Object.entries(tool.parameters || {}).map(([k]) => [k, { type: 'string' }])
        ),
        required: Object.keys(tool.parameters || {})
      }
    }
  }));
}

function buildHeaders(cfg, sessionId) {
  const headers = {
    'Content-Type': 'application/json',
    // OpenCode Go asks clients to identify themselves with their own user agent
    // instead of a generic SDK/HTTP-library name.
    'User-Agent': `xuanling/${VERSION} (+https://github.com/topelinux/xuan)`
  };

  if (cfg.llm.apiKey) {
    headers.Authorization = `Bearer ${cfg.llm.apiKey}`;
  }

  const provider = String(cfg.llm.provider || '').toLowerCase();
  const baseUrl = String(cfg.llm.baseUrl || '');
  if (provider === 'opencode-go' || baseUrl.includes('opencode.ai')) {
    // A stable per-conversation id lets OpenCode Go optimize routing and
    // prompt caching.
    headers['x-opencode-session'] = sessionId || 'xuanling';
  }

  return { ...headers, ...(cfg.llm.headers || {}) };
}

export async function callLLM(messages, tools, options = {}) {
  const cfg = getConfig();
  const payload = {
    model: cfg.llm.model,
    messages,
    tools: buildToolDefinitions(tools),
    tool_choice: 'auto'
  };

  const res = await fetch(`${cfg.llm.baseUrl}/chat/completions`, {
    method: 'POST',
    headers: buildHeaders(cfg, options.sessionId),
    body: JSON.stringify(payload)
  });

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`LLM API error: ${res.status} ${text}`);
  }

  const data = await res.json();
  return data.choices[0].message;
}
