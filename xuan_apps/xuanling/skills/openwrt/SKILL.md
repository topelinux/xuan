# OpenWrt Device Ops Skill

## Purpose
在 OpenWrt 设备上，通过玄灵对话完成诊断、日志、包安装与 xuan demo 验证。

## Environment Assumptions
- 玄灵运行在设备本机
- 设备具备 `xuan`
- 无 Node.js / npm 运行时（或不依赖它们）
- 优先使用 OpenWrt 工具链：`opkg`, `logread`, `ubus`

## Core Workflows

### 1) Doctor
目标：确认环境可执行后续任务。

步骤：
1. 检查 `xuan`, `opkg`, `ubus`, `logread`, `dmesg`
2. 输出平台信息（OpenWrt 识别、CPU、内存、load）
3. 给出缺失组件建议

### 2) Install JS Package
目标：安装 JS 依赖（例如 `dayjs`）。

步骤：
1. 在 workspace 下执行 `xuan npm install <package>`
2. 校验 `node_modules` 与锁文件状态
3. 输出安装结果与失败日志

### 3) Demo Validate
目标：快速验证安装包可被 xuan app 使用。

步骤：
1. 生成 `index_demo.js`
2. 运行 `xuan index_demo.js`
3. 汇报 stdout/stderr

### 4) Logs & Metrics
目标：收集现场信息。

步骤：
1. 日志优先级：`logread` -> `dmesg` -> `/var/log/messages` -> `/var/log/syslog`
2. 指标包括：load、内存、网卡、uptime

## Safety Policy
- 默认高风险动作需确认（安装、写文件、执行 shell/脚本）
- `--trust-all` 可跳过确认（CI 或受控环境）

## Prompting Guidance
当用户说“帮我安装 dayjs 并验证”时，执行顺序：
1. doctor_openwrt
2. xuan_npm_install(package=dayjs)
3. write_workspace_file(path=index_demo.js, content=...)
4. run_xuan_script(entry=index_demo.js)
5. 总结结果与下一步建议
