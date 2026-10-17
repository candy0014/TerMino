# 图形化迁移前的 Git 基线

2026-10-17 建立迁移基线。基线建立时工作区干净，`main` 与 `origin/main` 一致。

| 项目 | 值 |
| --- | --- |
| 仓库 | `git@github.com:candy0014/TerMino.git` |
| 提交 | `4d6e74c810877cf8801357d626f53c3f127f0979` |
| 提交说明 | Add seven challenges and fix challenge scoring and replay fidelity |
| 已发布标签 | `v2026.10.17` |
| 本地基线标签 | `baseline/pre-opengl-20261017` |
| 本地备份分支 | `backup/pre-opengl-20261017` |
| 本地完整历史备份 | `build-migration-backups/pre-opengl-20261017.bundle` |
| bundle SHA256 | `8016cfff78dea876496ab858cd2c4495d552456385b66328bb5fb14b1e89c1ad` |

基线发行版的 Linux 155 项和原生 Windows 154 项 CTest 均通过，两个发行包通过默认配置、SHA256、对局、回放和服务端启动检查。发行地址：<https://github.com/candy0014/TerMino/releases/tag/v2026.10.17>。

`build-migration-backups/` 是 Git 忽略的本地备份目录，需随本地工程保留。它另存基线信息 `baseline.json` 与本地 `.tetr` 的文件大小和 SHA256 记录 `local-tetr-before.json`。本地基线标签和备份分支尚未单独推送；已发布的 `v2026.10.17` 指向相同源码提交。

## 在独立目录检查基线

保留当前工作区和个人数据，在相邻目录建立旧版检出：

```sh
git worktree add --detach ../termino-pre-opengl baseline/pre-opengl-20261017
```

该目录使用独立的构建和运行数据。需要比较个人配置时先复制备份，不让验证程序写入现有 `.tetr`。

## 从本地备份恢复仓库

```sh
git bundle verify build-migration-backups/pre-opengl-20261017.bundle
git clone build-migration-backups/pre-opengl-20261017.bundle ../termino-restored
git -C ../termino-restored switch --detach v2026.10.17
```

## 回退后续改动

后续迁移按可独立回退的提交保存。遇到问题时，先保留未提交工作，再对有问题的迁移提交执行 `git revert <提交>`，形成可同步的回退记录。整个迁移需要回退时，按时间从新到旧撤销迁移提交，并运行终端完整回归。

不要用删除 `.tetr` 或清理个人数据的方式回退。Git 基线不包含配置、地图、预设、挑战成绩和录像；这些内容使用各自的备份恢复。

## 第二阶段之前的检查点

第一阶段提交为 `8422ab773e017a55d6939e5d945fe75e6595e31a`，标签
`migration/frontend-foundation-20261017`。第二阶段按一个独立提交保存，完成检查点为
`migration/opengl-preview-20261017`；仅撤销第二阶段时可对该标签所指的提交执行
`git revert migration/opengl-preview-20261017`，保留第一阶段和原终端全部内容。

只读比较可使用独立 worktree，不需要改动当前工作目录或个人数据：

```sh
git worktree add --detach ../termino-phase1 migration/frontend-foundation-20261017
```

第二阶段测试记录、构建日志及执行前的个人数据 SHA256 清单位于 Git 忽略的
`build-migration-backups/phase2-20261017/`。测试使用临时运行目录，禁止把个人 `.tetr`
加入提交或预览安装组件。

## 第三阶段检查点

第三阶段开始时 `main` 的提交为 `9a45ee9757cfe9b2677fd099f21c4243a35f97ea`，
第二阶段标签为 `migration/opengl-preview-20261017`。执行前的 Git 信息和 195 个本地
数据文件的 SHA256 已记录在 `build-migration-backups/phase3-20261017/`。

第三阶段作为独立提交保存，完成标签为 `migration/frontend-parity-20261017`。
只撤销本阶段时使用 `git revert migration/frontend-parity-20261017`；只读检查第二阶段：

```sh
git worktree add --detach ../termino-phase2 migration/opengl-preview-20261017
```

完成后的完整历史备份为 `build-migration-backups/phase3-20261017/frontend-parity.bundle`。
用 `git bundle verify` 校验后可在其他目录恢复。本地迁移标签和提交不表示 GitHub
已同步或已发布；正式四组合发行仍按第四阶段验收。
