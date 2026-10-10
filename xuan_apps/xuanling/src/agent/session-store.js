import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { join } from 'node:path';

export class SessionStore {
  constructor(basePath) {
    this.basePath = basePath;
    this.cache = new Map();
  }

  async initialize() {
    await mkdir(this.basePath, { recursive: true }).catch(() => {});
  }

  filePath(sessionKey) {
    return join(this.basePath, `${sessionKey}.json`);
  }

  async load(sessionKey) {
    if (this.cache.has(sessionKey)) return this.cache.get(sessionKey);

    try {
      const raw = await readFile(this.filePath(sessionKey), 'utf-8');
      const parsed = JSON.parse(raw);
      const messages = Array.isArray(parsed.messages) ? parsed.messages : [];
      this.cache.set(sessionKey, messages);
      return messages;
    } catch {
      const empty = [];
      this.cache.set(sessionKey, empty);
      return empty;
    }
  }

  async save(sessionKey) {
    const messages = this.cache.get(sessionKey) || [];
    const payload = {
      key: sessionKey,
      updatedAt: Date.now(),
      messages: messages.slice(-60)
    };
    await writeFile(this.filePath(sessionKey), JSON.stringify(payload, null, 2), 'utf-8');
  }

  async append(sessionKey, role, content) {
    const messages = await this.load(sessionKey);
    messages.push({ role, content });
    if (messages.length > 60) messages.splice(0, messages.length - 60);
    await this.save(sessionKey);
  }
}
