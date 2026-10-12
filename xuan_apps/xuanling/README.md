# 玄灵 (XuanLing)

玄灵是运行在设备本机的 **对话式 AI agent**，用于辅助用户使用 xuan 完成设备诊断、日志收集、JS 包安装与示例验证。

## 目标场景

- OpenWrt 等嵌入式设备
- 用户通过自然语言发起任务，例如：
  - 帮我检查设备状态并收集日志
  - 帮我安装 dayjs，然后写个 xuan demo 验证

## 使用 OpenCode Go（推荐）

[OpenCode Go](https://opencode.ai/docs/go/) 提供 OpenAI 兼容接口，玄灵内置
`opencode-go` provider 预设，可直接调用 `deepseek-v4.1-flash` 等模型：

```bash
export OPENCODE_API_KEY=<你的 key>
cp xuan_apps/xuanling/config.opencode-go.example.json xuan_apps/xuanling/config.json

./build/xuan run xuan_apps/xuanling/bin/xuanling.js ask "帮我检查设备状态并收集日志" \
  --config xuan_apps/xuanling/config.json
```

也可以完全用环境变量（会覆盖配置文件中的 llm 段）：

```bash
LLM_PROVIDER=opencode-go LLM_MODEL=deepseek-v4.1-flash OPENCODE_API_KEY=<key> \
  ./build/xuan run xuan_apps/xuanling/bin/xuanling.js ask "..."
```

支持的 llm 环境变量：`LLM_PROVIDER`、`LLM_MODEL`、`LLM_BASE_URL`、`LLM_API_KEY`。
任何 OpenAI 兼容服务（Ollama、DashScope、自建网关等）都可以通过
`llm.baseUrl` + `llm.model` + `llm.apiKey` 配置。

## 运行方式

### 1) 准备配置

```bash
cp xuan_apps/xuanling/config.example.json xuan_apps/xuanling/config.json
# 编辑 config.json，填入 llm.apiKey
```

### 2) 启动对话

```bash
./build/xuan run xuan_apps/xuanling/bin/xuanling.js chat \
  --config xuan_apps/xuanling/config.json \
  --workspace /tmp/xuanling-workspace
```

默认高风险操作需要确认。

### 3) 一次性提问（非交互）

```bash
./build/xuan run xuan_apps/xuanling/bin/xuanling.js ask "帮我安装 dayjs 并做 xuan demo 验证" \
  --config xuan_apps/xuanling/config.json
```

## 安全策略

- 默认：安装包、写文件、shell 执行、脚本运行都要确认
- 可选：`--trust-all` 跳过确认

```bash
./build/xuan run xuan_apps/xuanling/bin/xuanling.js chat --trust-all
```

## 内置工具（供 agent 调用）

- `doctor_openwrt`
- `collect_metrics`
- `collect_logs`
- `xuan_npm_install`
- `write_workspace_file`
- `read_workspace_file`
- `list_workspace_files`
- `run_shell`
- `run_xuan_script`

## Skill 文档

- OpenWrt skill: `xuan_apps/xuanling/skills/openwrt/SKILL.md`

## 当前状态

这是 MVP 版本，重点先保证对话式流程闭环：
1. 诊断
2. 安装
3. 生成 demo
4. 运行验证
5. 回传结果
