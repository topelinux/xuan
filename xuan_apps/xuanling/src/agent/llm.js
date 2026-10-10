import { getConfig } from './config.js';

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

export async function callLLM(messages, tools) {
  const cfg = getConfig();
  const payload = {
    model: cfg.llm.model,
    messages,
    tools: buildToolDefinitions(tools),
    tool_choice: 'auto'
  };

  const res = await fetch(`${cfg.llm.baseUrl}/chat/completions`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${cfg.llm.apiKey}`
    },
    body: JSON.stringify(payload)
  });

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`LLM API error: ${res.status} ${text}`);
  }

  const data = await res.json();
  return data.choices[0].message;
}
