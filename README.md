# ascend-monitor

`ascend-monitor` 是一个基于 Qt 的桌面图形工具，用于查看 Ascend NPU 卡信息、Card/Chip 到逻辑 NPU 的映射关系、实时监控指标，以及宿主机上的 NPU 进程信息。

## 功能特性

- 卡列表：来自 `npu-smi info -l`
- 卡与芯片映射表：来自 `npu-smi info -m`
- 卡健康状态与内存汇总：来自 `npu-smi info -t health -i <Card ID>` 和 `npu-smi info -t memory -i <Card ID>`
- 实时图表监控（1 秒刷新）：
	- 芯片温度：`npu-smi info -t temp -i <Card ID>`
	- AICore/AIVector/AICPU 利用率：`npu-smi info -t usages -i <Card ID>`
	- 卡功耗：`npu-smi info -t power -i <Card ID>`
- `pro` 进程监控页：
	- 支持按选中 Card ID 展示进程
	- 未选中时展示所有设备进程
	- 进程数据解析自 `npu-smi info`
	- 每秒自动刷新
- 远程采集支持（SSH）：可连接仅在远端可用 `npu-smi` 的主机
- 版本显示：左上角显示 Logo，右上角显示 `npu-smi -v` 解析到的版本号（获取失败时不显示）
- 启动单实例限制：防止重复启动程序
- 默认自动刷新：启动后自动采集，监控数据持续刷新

## 环境要求

- CMake 3.16 及以上
- Qt 5 Widgets 或 Qt 6 Widgets 开发包
- Qt Charts（例如 `qt6-qtcharts`）
- `npu-smi` 命令可在 `PATH` 中访问

## 编译

```bash
cmake -S . -B build
cmake --build build
```

## RPM 打包

### 本地打包

```bash
cmake -S . -B build-rpm -DCMAKE_BUILD_TYPE=Release
cmake --build build-rpm
cpack --config build-rpm/CPackConfig.cmake -G RPM
```

生成的 RPM 位于 [build-rpm](build-rpm) 目录下（包名为 `ascend-monitor`）。

### 远程打包与验证（10.40.125.244）

已支持通过免密 SSH 在远程环境打包和验证：

```bash
./scripts/package_rpm_remote.sh
```

可选参数：

```bash
./scripts/package_rpm_remote.sh <remote_host> <remote_dir> <local_output_dir>
```

示例：

```bash
./scripts/package_rpm_remote.sh 10.40.125.244 ~/ascend-monitor-rpm dist-rpm
```

脚本会执行以下步骤：

- 同步项目到远程目录
- 远程构建 Release
- 远程执行 `cpack -G RPM`
- 远程执行 `rpm -qpi` 与 `rpm -qpl` 做基本包校验
- 将生成的 RPM 拉回本地输出目录（默认 [dist-rpm](dist-rpm)）

## 运行

```bash
./build/ascend-monitor
```

## 使用说明

- 工具栏支持本地/远程互斥模式，默认本地模式。
- 远程模式下可输入 `User`、`Host`、`Password`，点击“连接”后开始远程采集与监控。
- 远程功能仅在 Debug 构建中显示并可用；非 Debug 构建会隐藏远程相关控件。
- 在已验证远程主机 `127.0.0.1` 上，可用 Card ID 是 `npu-smi info -l` 返回的 `NPU ID`（如 `3`、`4`、`5`、`6`）。

<!-- atomgit-points: daily update 2026-09-28 -->
