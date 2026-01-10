---
allowed-tools: Bash(git add:*), Bash(git status:*), Bash(git diff:*), Bash(git branch:*), Bash(git log:*), Bash(git commit:*)
description: Create a git commit (English instructions; Chinese multi-section commit message)
---

## Context

- Current git status: !`git status`
- Current git diff (staged and unstaged changes): !`git diff HEAD`
- Current branch: !`git branch --show-current`
- Recent commits: !`git log --oneline -10`

## Your task

1. **Analyze the changes**
   - Read `git diff HEAD` (both staged and unstaged) and infer the **intent** and **scope**:
     - Is this a new feature, bug fix, documentation update, refactor, performance improvement, test change, build/CI change, dependency update, or revert?
     - Does it include any breaking changes or require migration notes?
   - If the diff contains multiple independent themes, group them into multiple **sections** (see “Sectioning rules”).

2. **Craft ONE high-quality commit message (mandatory multi-line, sectioned)**
   - Use **Conventional Commits** for the subject line:
     - `feat: ...` / `fix: ...` / `docs: ...` / `refactor: ...` / `perf: ...` / `test: ...` / `build: ...` / `ci: ...` / `chore: ...` / `revert: ...`
   - The subject line must be concise and capture the primary change.
   - The body must be **multi-line** and must organize **all changes** into sensible **sections**. A single section is fine if all changes are tightly related.

3. **Prepare the index (stage changes)**
   - If there are unstaged changes, stage them with `git add`.
   - If changes should clearly be split into multiple commits (unrelated concerns, different rollback needs, risky mixed refactors), **stop** and propose a split plan (which files/changes go into which commit) instead of committing.

4. **Commit**
   - Before running `git commit`, print the **final commit message** (full multi-line text) for review.
   - Then run `git commit` with that message.
   - Do **NOT** add any co-authorship footer.

## Sectioning rules (key requirement)

- The commit message must follow this structure:

  Conventional Commit subject (required)
  
  (blank line)
  
  ### <Section title 1>
  - 1–3 bullets describing what changed and why/impact.
  
  ### <Section title 2>
  - ...

- Choose section titles that best reflect the changes, e.g.:
  - `### 功能` / `### 修复` / `### 重构` / `### 文档` / `### 测试` / `### 构建与 CI` / `### 依赖` / `### 性能` / `### 安全` / `### 兼容性`
  - Or by module/path when clearer: `### api` / `### ui` / `### build` / `### openwrt` ...
- Each section must map to concrete diff content—avoid vague claims.
- If user-visible behavior/config/interface changes exist, explicitly state them in the relevant section.

## Breaking changes

- If there is a breaking change:
  - Use `feat!: ...` or `refactor!: ...` in the subject.
  - Add a dedicated section:
    - `### 破坏性变更`
    - Include impact and migration steps.

## Constraints

- **The final commit message must be written in Chinese**, including the subject and all section content.
- No emojis, no marketing tone.
- Only use the allowed tools: `git status`, `git diff`, `git branch`, `git log`, `git add`, `git commit`.
