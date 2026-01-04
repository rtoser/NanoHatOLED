# 事故报告：.mcp.json 敏感信息泄露

## 概述

| 项目 | 内容 |
|------|------|
| 发生时间 | 2026-01-04 |
| 影响范围 | Notion API Token 泄露至 GitHub 公开仓库 |
| 严重程度 | 中等（API Token 可被滥用访问 Notion 数据） |
| 处理状态 | 已解决 |

## 问题描述

在执行 `git push` 时，GitHub Push Protection 拦截了推送请求：

```
remote: error: GH013: Repository rule violations found for refs/heads/master.
remote: - GITHUB PUSH PROTECTION
remote:   - Push cannot contain secrets
remote:   —— Notion API Token ——————————————————————————————————
remote:      locations:
remote:        - commit: 11d3071f054e813cc9816385913b35759e5a2d4e
remote:          path: .mcp.json:22
```

## 根本原因

### 1. MCP 配置文件被意外提交

`.mcp.json` 是 Claude Code 的 MCP (Model Context Protocol) 服务器配置文件，包含各种服务的 API 密钥：

```json
{
  "mcpServers": {
    "notion": {
      "command": "npx",
      "args": ["-y", "@anthropic/notion-mcp-server"],
      "env": {
        "NOTION_API_KEY": "ntn_xxxx..."  // 敏感信息
      }
    }
  }
}
```

### 2. .gitignore 配置缺失

项目的 `.gitignore` 中没有包含 `.mcp.json`，导致该文件在 commit 11d3071 中被意外加入版本控制。

### 3. 后续 commit 未能清除

虽然在后续 commit (00b8f72) 中尝试排除该文件，但：
- 文件已经存在于 Git 历史中
- 仅修改 `.gitignore` 无法从历史中删除已提交的文件

## 解决方案

### 步骤 1：将 .mcp.json 添加到 .gitignore

```bash
echo -e "\n# MCP config (contains secrets)\n.mcp.json" >> .gitignore
git add .gitignore
git commit -m "chore: add .mcp.json to gitignore"
```

### 步骤 2：使用 git filter-branch 从历史中彻底删除

```bash
git filter-branch --force --index-filter \
  'git rm --cached --ignore-unmatch .mcp.json' \
  --prune-empty --tag-name-filter cat -- --all
```

**参数说明**：
- `--force`：强制执行，即使已有备份
- `--index-filter`：只修改索引，不 checkout 文件（更快）
- `git rm --cached --ignore-unmatch .mcp.json`：从索引中删除文件
- `--prune-empty`：删除变成空的 commit
- `--tag-name-filter cat`：保持 tag 名称不变
- `-- --all`：处理所有分支和 tag

### 步骤 3：强制推送到远程

```bash
git push --force origin master
```

### 步骤 4：清理本地 Git 仓库

```bash
# 删除 filter-branch 创建的备份引用
rm -rf .git/refs/original/

# 清理 reflog
git reflog expire --expire=now --all

# 执行垃圾回收，彻底删除悬空对象
git gc --prune=now --aggressive
```

### 步骤 5：轮换泄露的凭证

**重要**：即使已从 Git 历史中删除，泄露的 Token 仍应视为已暴露：

1. 登录 [Notion Integrations](https://www.notion.so/my-integrations)
2. 找到对应的 Integration
3. 点击 "Regenerate" 生成新的 API Token
4. 更新本地 `.mcp.json` 中的 Token

## 预防措施

### 1. 全局 .gitignore

在用户级别配置全局 gitignore：

```bash
# 创建全局 gitignore
echo ".mcp.json" >> ~/.gitignore_global

# 配置 Git 使用该文件
git config --global core.excludesfile ~/.gitignore_global
```

### 2. 项目 .gitignore 模板

每个新项目应包含常见的敏感文件：

```gitignore
# IDE/Editor configs with potential secrets
.mcp.json
.env
.env.local
*.pem
*.key
credentials.json
```

### 3. pre-commit 钩子

使用 [detect-secrets](https://github.com/Yelp/detect-secrets) 或类似工具在提交前检测敏感信息：

```bash
# 安装
pip install detect-secrets

# 初始化 baseline
detect-secrets scan > .secrets.baseline

# 配置 pre-commit hook
```

### 4. 启用 GitHub Secret Scanning

在仓库设置中启用 Secret Scanning：
- Settings → Security & analysis → Secret scanning → Enable

## 替代方案对比

| 方案 | 优点 | 缺点 |
|------|------|------|
| git filter-branch | Git 内置，无需安装 | 较慢，语法复杂 |
| git filter-repo | 更快，更安全 | 需要额外安装 |
| BFG Repo-Cleaner | 简单易用，速度快 | 需要 Java 环境 |
| GitHub 允许推送 | 最快 | Token 仍在历史中 |

本次选择 `git filter-branch` 因为：
- 无需安装额外工具
- 历史记录不大，性能可接受
- 完全从历史中删除敏感信息

## 相关资源

- [GitHub: Removing sensitive data from a repository](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)
- [git-filter-branch documentation](https://git-scm.com/docs/git-filter-branch)
- [BFG Repo-Cleaner](https://rtyley.github.io/bfg-repo-cleaner/)
- [git-filter-repo](https://github.com/newren/git-filter-repo)
