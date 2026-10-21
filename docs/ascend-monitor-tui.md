# ascend-monitor-tui

`ascend-monitor-tui` 在 Ascend 主机本地运行，以 C++、ncurses、DCMI 和 `/proc` 提供设备、主机历史和进程监控。运行不需要 Python、Qt、图形桌面；现有 `ascend-monitor` GUI 保留。

## 构建和运行

源码构建需要支持 C++17 的编译器、CMake 3.16+、`ncurses-devel` 开发包，以及驱动自带的 `dcmi_interface_api.h`。

```sh
cmake -S . -B build-top \
  -DASCEND_MONITOR_BUILD_GUI=OFF \
  -DASCEND_MONITOR_BUILD_TUI=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-top -j4
ctest --test-dir build-top --output-on-failure
./build-top/ascend-monitor-tui
```

默认同时构建 GUI 和 TUI；只构建 GUI 可使用 `-DASCEND_MONITOR_BUILD_TUI=OFF`。DCMI 头文件默认查找 `/usr/local/Ascend/driver/include`，也可通过 `-DDCMI_INCLUDE_DIR=/path/to/include` 指定。开发机没有驱动时，可使用 `-DASCEND_MONITOR_TUI_DCMI=OFF` 编译并运行核心测试；此构建不会提供 NPU 读数。

运行时优先从 `/usr/local/Ascend/driver/lib64/driver/libdcmi.so` 加载驱动，再使用系统动态库搜索路径查找。无法加载或初始化时显示错误；不支持的字段显示 `N/A`。

```sh
ascend-monitor-tui --interval 1
ascend-monitor-tui --json
ascend-monitor-tui --ascii
ascend-monitor-tui --help
```

`--json` 采集两轮后输出一个快照，CPU 使用率由两轮差值计算。缺失数值为 JSON `null`；无设备或设备枚举不完整时退出码为 1，参数错误为 2。

## 界面和操作

界面依次显示时间及版本、每芯片设备行与进度条、主机及 NPU 汇总历史、设备进程表。设备区采用带竖线的四组分栏：设备/温度/功耗、PCI/内存、Card/Chip、内存与 AI Core 进度条。90 列及以上均保持两行设备内容与进度条并排；历史区带外框及中间分隔线，进程区带独立边框。最低可用尺寸为 90 列 × 24 行，较矮窗口缩减表头并使用滚动保留可见数据。颜色沿用终端主题，进度条以 50% / 80% 为黄 / 红阈值。

| 按键 | 操作 |
| --- | --- |
| `h` / `?` | 打开/关闭帮助，仍继续采样 |
| `q` | 主界面退出；帮助/详情页返回 |
| `Tab` | 切换设备 / 进程列表焦点 |
| 上下箭头、`j` / `k` | 进程焦点时移动选择；设备焦点时滚动列表 |
| PageUp / PageDown、Home / End | 翻页或到首尾 |
| 左右箭头 | 横向查看长命令行 |
| 鼠标左键点击 `NPU-MEM` / `%MEM` / `%CPU` 表头 | 切换排序列时默认降序，再点当前列切换升降序 |
| `s` / `S` | 按 NPU 内存 → 主机内存 → CPU 循环切换排序列，每次切换恢复降序 |
| `/` | 反转当前排序方向 |
| 鼠标左键点击进程行 | 选中进程；点击排序表头以外的非进程区域取消选择 |
| `Enter` | 打开选中进程的详情；详情页内返回进程列表 |
| `Esc` | 帮助/详情返回；列表中先取消选择，无选择时退出 |

帮助页将操作与指标分开：`1 Shortcuts` 按导航、进程选择、排序、返回/退出分组；`2 Metrics` 解释设备标识、指标和历史口径。帮助内按 `1` / `2`、`Tab` / `Shift-Tab`、左右箭头或点击页签切页；上下箭头、j/k、PageUp/PageDown、Home/End 滚动当前页。按 `Esc` / `h` / `?` / `q` 返回打开帮助前的界面。按键列与说明列对齐，长说明自动换行；内容最大宽度 108 列，宽终端居中。底部固定显示操作提示、当前页与可见行范围，小窗口可滚动阅读全部内容，缩放不清空监控历史。

进程默认按 NPU 内存全表降序排列，支持的排序列仅为 `NPU-MEM`、`%MEM` 和 `%CPU`。当前排序列表头高亮，`▼` 表示降序、`▲` 表示升序；ASCII 模式或三角字符宽度不适合时使用 `v` / `^`。进程区标题提示可点击表头或按 `s` 排序，无需常驻底栏。鼠标点击包括该列表头文字及其后的列内空白；其他表头、数据行、帮助页和边框不触发排序。终端需支持鼠标上报，不支持时可用键盘。

未选择进程时，切换排序回到进程表顶部；已有选择时保持目标可见。自动刷新和窗口缩放保留排序状态。缺失值在升序、降序中均置于最后，真实零值正常参与排序；相同数值按 PID、物理 NPU ID、Card/Chip 升序确定先后。`%MEM` 按已有的主机内存占比排序，同一宿主机上与 RSS 排序等价。同一 PID 跨芯片仍显示多行，各芯片的设备内存独立参与排序，CPU 和主机内存不重复累加。排序仅作用于交互界面，不改变设备顺序或 JSON 快照的原始顺序。

选中进程行使用反显，其关联的所有 NPU 条目保留原有颜色，其他 NPU 的文字和进度条保持原色并变淡，保留绿/黄/红所表达的负载含义；边框和整机历史不变。进度条使用前景色块填充（Unicode 为 █，ASCII 为 #），避免反显空格导致背景色无法正常变淡。默认无选中行。刷新和排序后按进程身份保持选择并自动滚动到可见区域；同一进程的某条设备占用行消失时，优先跟踪它仍存在的其他设备行。进程身份使用 PID 和 `/proc` starttime，无法验证身份时不能新建选择，避免 PID 重用后关联错误的进程。上下键、Home/End 和翻页会跳过无法验证身份的行。Tab 切换到设备焦点后，仍可独立滚动设备列表。

进程详情提供 `NPU Memory`、`CPU Usage`、`Host Memory (RSS)` 三张历史图，显示实时占用，以 `Y-axis` 标注各图的纵坐标范围。`NPU Memory` 合计该进程在各芯片的设备内存；CPU 和主机 RSS 只取一份，不按设备行重复累加。`Host Memory (RSS)` 同时显示 RSS 容量和占主机内存的比例。CPU 以单逻辑 CPU 满载为 100%，图表量程可随多线程使用率扩展到大于 100%；容量图按字节自动缩放，不混用容量与利用率。

历史从选中该进程时开始积累，使用现有采样线程和固定窗口平均，保留最近 600 秒；打开帮助或详情不会中断采样，返回列表后再次进入同一目标保留历史，切换到其他进程则清空。缺失读数和中断窗口留空。某些设备的进程列表或该进程设备内存不可用时，NPU 合计显示 N/A，避免将部分合计当成完整值。目标退出、释放所有 NPU 或 PID 被重用后，详情保留已有历史、将当前值标为不可用，不跟踪到新进程。详情页 `Esc` / `q` / `Enter` 均返回列表；主界面 `q` 仍退出。

图表默认使用 Unicode 盲文点阵（每格 2×4 点），不再使用星号。启动时按当前 locale 初始化；UTF-8 不可用、locale 无效或盲文字符宽度不是单格时自动使用 ASCII 图形。`--ascii` 强制使用 `.` / `:` 曲线和 `+ - |` 边框，帮助页显示当前模式。字体缺字无法自动判断，出现方框时可手动切换。主机内存从上沿向下填充，Swap 从下沿向上填充；零值为基线点，缺失值留空。

已有构建目录若缓存了普通 ncurses 库，重新配置时使用 `cmake -U 'CURSES_*' -S . -B build-top` 清除缓存，保留原有构建选项。

主机与 NPU 历史采用统一时间尺度，默认尽量每 0.5 秒采集一次，每 1 秒将窗口内读数取平均后生成一个历史点；驱动调用较慢时按实际完成速度采样，不补采。`--interval` 控制历史平均窗口（JSON 模式仍控制两轮采集间隔）；采集目标为 `min(0.5, interval/2)` 秒。每个盲文小点或 ASCII 格对应一个历史窗口。只有已完成窗口进入图表，已显示点不可修改；窗口内某指标出现缺测，则该指标的整个窗口留空，没有采样的窗口也留空，不插值。实时数值仍来自最新快照。图表宽度决定显示范围，保留最近 600 秒，缩放不清空历史。刻度固定为 30/60/120/180…秒，宽度不足时省略。完整历史区采用上方 5 行、时间轴 1 行、下方 5 行；主机内存占下方前 4 行，Swap 位于最底行，文字覆盖对应绘图区。矮窗口压缩图表高度。仅发生采集错误时在底部显示错误信息，超过 3 秒未得到新快照时标记 STALE。

## 指标口径

- **NPU**：物理 NPU ID，按此排序；另外展示逻辑 ID 和 Card/Chip，映射失败不会拿其他编号代替。
- **Share**：指定芯片的容器共享使能状态，使用 `dcmi_get_device_share_enable` 查询；`On` 表示启用，`Off` 表示禁用，接口缺失、不支持、查询失败或返回异常值时为 `N/A`。它表示功能开关，不表示当前有多少容器共享。首次采集读取，之后每芯片每 5 秒刷新，期间复用缓存；失败后清除旧值并按同样周期重试。JSON 字段 `device_share` 为 `true` / `false` / `null`。主表保留 Logic ID，Card/Chip 后显示 Share；不足 100 列时 Card/Chip 缩写为 `3/0`，保留完整状态。
- **内存**：本版验证的是 310P3 DDR，容量单位为 MiB/GiB；DCMI 的 MiB 转为字节，已用为总量减可用量。容量占用与内存带宽利用率是不同指标。HBM 型号尚未适配，不将 DDR 当作 HBM。
- **功耗**：仅芯片功耗，DCMI 的 0.1 W 转为 W。目标机此接口不支持，因此为 `N/A`；不会重复显示整卡 MCU 功耗充当芯片功耗。
- **NPU 汇总内存**：可读芯片已用容量之和 / 总容量之和；**汇总 AI Core**：可读芯片利用率的算术平均。两者均显示可读芯片数 / 总芯片数。
- **主机 CPU**：全机 0–100%，按 `/proc/stat` 差值计算，idle 和 iowait 算空闲，不重复计入 guest。
- **进程 CPU**：一个逻辑 CPU 满载为 100%，多线程可超过 100%。首轮、进程退出、PID 重用时不可用。
- **主机内存**：总量减 MemAvailable；**Swap**：总量减空闲，未配置 Swap 为 0%。
- **进程 %MEM**：RSS / 主机总内存；**TIME**：自启动后的运行时长，不是累计 CPU 时间。
- **进程行**：按设备关联展示。同一 PID 在多个芯片上的设备内存分别保留，主机 CPU/RSS 只采一次并复用。读取不了 `/proc` 时该行的主机字段为 `N/A`。

## RPM

```sh
(cd build-top && cpack -G RPM)
sudo rpm -Uvh build-top/ascend-monitor-tui-0.1.0-9.aarch64.rpm
ascend-monitor-tui
```

联合构建生成 `ascend-monitor` 和 `ascend-monitor-tui` 两个独立 RPM，架构取构建目标，不再固定为 x86_64。TUI 包只包含 `/usr/bin/ascend-monitor-tui`，依赖系统 C/C++ 运行库及 ncurses，不依赖 Qt 或 Python。Ascend 驱动需在目标机器上预先安装；通过动态加载使用，不打包驱动库或头文件。

## 验证

核心测试覆盖容量加权、缺失值与零值、历史保留、主机 CPU、复杂进程名称、跨设备 PID、PID 重用和退出。真实 NPU 主机还可运行下面的 PTY 验收脚本；Python 仅用于测试，不是程序依赖。脚本的 8 芯片、310P3、版本断言对应本次验收主机。

```sh
python3 tests/top/pty_smoke.py ./build-top/ascend-monitor-tui --output /tmp/ascend-monitor-tui-evidence
```

排序核心测试 `test_process_sort` 随 CTest 运行，覆盖三列全表排序、零值与缺失值、跨芯片 PID、同值顺序及表头点击边界。另可运行不依赖 NPU 读数的鼠标与键盘交互验收，覆盖 Unicode / ASCII、90/120/200 列、帮助页和缩放后的表头位置：

```sh
python3 tests/top/pty_sort.py ./build-top/ascend-monitor-tui
```

进程选择与详情的核心测试 `test_process_view` 随 CTest 运行。独立测试程序提供确定性数据，可验证跨芯片联动、灰色样式、CPU 大于 100%、缺测和 PID 重用；它只属于测试构建，不会安装到系统，也不会操作真实进程：

```sh
python3 tests/top/pty_process_view.py ./build-top/test_process_view_tui --fixture --output /tmp/process-view-fixture
```

真实 NPU 主机可使用同一脚本验证选择、置灰、详情与缩放，不触发退出/重用模拟：

```sh
python3 tests/top/pty_process_view.py ./build-top/ascend-monitor-tui --output /tmp/process-view-hardware
```

帮助布局的独立 PTY 验收覆盖页签与鼠标点击、对齐和样式、长文本换行、90×24 滚动、宽屏居中、过小窗口恢复以及关闭帮助返回原界面：

```sh
python3 tests/top/pty_help.py ./build-top/test_process_view_tui --output /tmp/help-layout
```
