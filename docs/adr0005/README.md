# ADR0005 文档索引

本目录集中存放 ADR0005 相关的新文档，避免与旧设计文档混淆。

## 核心设计

- [ADR 0005：终极三线程架构](../adr/0005-ultimate-threaded-architecture.md)
- [线程模型细化](thread-model.md)
- [新人入门指南](onboarding.md)

## HAL 说明

- [GPIO HAL 说明（libgpiod + mock）](gpio-hal.md)

## 测试与实施

- [测试架构设计](testing-architecture.md)
- [重新实现计划（分阶段）](implementation-plan.md)

## 设计规格

- [Phase 1-2 实现设计](../design/phase1-2-implementation.md)

## 当前进度

- Phase 1：已完成（Host）
- Phase 2：已完成（Host），Target 基础验证通过（gpiochip1:0/2/3）

## 迁移说明

- 旧实现保留在 `src_old/`，新实现从 `src/` 重新开始。
