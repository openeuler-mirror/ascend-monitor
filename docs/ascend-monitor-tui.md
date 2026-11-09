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

`--interval` 表示一轮采集完成后，到开始下一轮前的等待时间，默认 1 秒，可设为 0.2–60 秒。采集本身也需要时间，因此两轮开始时间的间隔大于该值；不补采。`--json` 同样在两轮采集之间等待 `--interval` 秒，再输出一个快照，CPU 使用率由两轮差值计算。缺失数值为 JSON `null`；无设备或设备枚举不完整时退出码为 1，参数错误为 2。

## 界面和操作

界面依次显示时间及版本、每芯片设备行、主机及 NPU 汇总历史、设备进程表。最低可用尺寸为 90 列 × 24 行，布局先为进程表保留至少 5 行数据空间，再分配设备视口；数据不足时只显示已有进程。默认 Auto 在完整详细设备区仍能保留进程空间时显示两行设备数据及一行分隔线，否则自动切换单行 Compact。详细布局分为设备/温度/功耗、PCI/内存、Card/Chip/Share、内存与 AI Core 进度条；Compact 显示物理 NPU ID、型号、内存已用/总量、CORE、温度和功耗，移除设备之间的分隔线与进度条。

按 `c` 在 Auto 与手动 Compact 之间切换，仅本次运行生效。手动 Compact 在窗口增高后仍保持单行设备；Auto 可恢复详细布局。版本行显示 `Auto/Detailed`、`Auto/Compact` 或 `Compact`，并显示当前设备范围与总数。视口外的设备用 Tab 切换焦点后滚动查看，PageUp/PageDown 按设备视口翻页。高度小于 32 行且完整历史区无法容纳全部单行设备及最低进程空间时，历史区折叠为两行摘要，保留 CPU、Load1、主机内存、Swap、NPU MEM 与 NPU CORE。后台仍保留历史，空间恢复后重新显示曲线。切换模式、缩放、暂停均保留进程选择。

颜色沿用终端主题，内存与 AI Core 的数值和进度条分别按各自指标着色，以 50% / 80% 为黄 / 红阈值；不可用指标和设备身份信息使用默认色。温度、功耗使用默认色。设备区用 `CORE` 表示 AI Core 利用率，汇总区用 `NPU CORE` 表示可读芯片的平均 AI Core 利用率。

| 按键 | 操作 |
| --- | --- |
| `h` / `?` | 打开/关闭帮助，仍继续采样 |
| `q` | 主界面退出；帮助/详情页返回 |
| `Tab` | 切换设备 / 进程列表焦点 |
| `+` / `=`、`-` | 主界面与详情页增大 / 减小采集后的等待，每次 0.1 秒，范围 0.2–60 秒 |
| `Space` | 冻结 / 恢复显示；后台继续采样 |
| `c` | 主界面切换 Auto / Compact 设备布局，仅本次运行生效 |
| 上下箭头、`j` / `k` | 进程焦点时移动选择；设备焦点时滚动列表 |
| PageUp / PageDown、Home / End | 翻页或到首尾 |
| 左右箭头 | 横向查看长命令行 |
| 鼠标左键点击 `NPU-MEM` / `%MEM` / `%CPU` 表头 | 切换排序列时默认降序，再点当前列切换升降序 |
| `s` / `S` | 按 NPU 内存 → 主机内存 → CPU 循环切换排序列，每次切换恢复降序 |
| `/` | 反转当前排序方向 |
| `f` | 输入关键字，实时搜索并高亮命令行、用户名或 PID 匹配的进程；英文字母不区分大小写 |
| 鼠标左键点击进程行 | 选中进程；点击排序表头以外的非进程区域取消选择 |
| `Enter` | 打开选中进程的详情；详情页内返回进程列表 |
| `Esc` | 帮助/详情返回；列表中依次清除搜索、取消选择，均无时退出 |

顶部常驻显示 `LIVE wait 1.0s`，到达上下限时显示 `[min]` / `[max]`。`wait` 是采集结束后的等待，版本信息行的 `collect` 是实际采集耗时。调整仅在本次运行生效，不写入配置；正在执行的采集正常完成。等待中的采样器按“上轮采集结束时间 + 新等待”重算截止时间，已到期时开始下一轮，不补采。

按 `Space` 后，顶部以黄色加粗显示 `PAUSED @HH:MM:SS`，时间为冻结快照的采集完成时刻。设备读数、进程表和历史图一起冻结，仍可滚动、搜索、排序和查看详情；详情也读取冻结数据。暂停期间新选目标的详情仅有这份冻结快照，不伪造此前历史。后台继续采样，也可调整等待；恢复时直接显示最新快照与最近 600 秒历史，不逐帧补播。暂停不会因冻结快照变旧而显示 STALE；主界面与详情页共用暂停状态，帮助页保留该状态。

按 `f` 进入搜索输入，底部显示关键字和匹配的设备进程占用行数；同一 PID 跨芯片出现多行时分别计数。搜索按完整命令行、用户名或 PID 的字面子串匹配，不使用正则表达式，不隐藏未匹配进程。输入时所有匹配行加粗并显示为黄色，即使命令行匹配部分在可见区域之外也会高亮整行；每次输入变化定位到当前排序下的首个匹配行，身份可验证时同时选中该行。`Backspace` 删除一个字符，`Ctrl-U` 清空输入，`Enter` 结束输入并保留高亮，再按 `f` 可继续编辑，`Esc` 清除搜索。输入期间 `q`、`s`、`h`、`c`、`/`、`+`、`-`、空格等均作为关键字文字；结束输入后恢复快捷键。搜索最长 256 字节，自动刷新、排序、帮助和缩放均保留关键字并重新计算高亮；采样持续进行。

帮助页将操作与指标分开：`1 Shortcuts` 按导航、进程选择、排序、返回/退出分组；`2 Metrics` 解释设备标识、指标和历史口径。帮助内按 `1` / `2`、`Tab` / `Shift-Tab`、左右箭头或点击页签切页；上下箭头、j/k、PageUp/PageDown、Home/End 滚动当前页。按 `Esc` / `h` / `?` / `q` 返回打开帮助前的界面。按键列与说明列对齐，长说明自动换行；内容最大宽度 108 列，宽终端居中。底部固定显示操作提示、当前页与可见行范围，小窗口可滚动阅读全部内容，缩放不清空监控历史。

进程默认按 NPU 内存全表降序排列，支持的排序列仅为 `NPU-MEM`、`%MEM` 和 `%CPU`。当前排序列表头高亮，`▼` 表示降序、`▲` 表示升序；ASCII 模式或三角字符宽度不适合时使用 `v` / `^`。进程区标题提示可点击表头或按 `s` 排序，无需常驻底栏。鼠标点击包括该列表头文字及其后的列内空白；其他表头、数据行、帮助页和边框不触发排序。终端需支持鼠标上报，不支持时可用键盘。

未选择进程时，切换排序回到进程表顶部；已有选择时保持目标可见。自动刷新和窗口缩放保留排序状态。缺失值在升序、降序中均置于最后，真实零值正常参与排序；相同数值按 PID、物理 NPU ID、Card/Chip 升序确定先后。`%MEM` 按已有的主机内存占比排序，同一宿主机上与 RSS 排序等价。同一 PID 跨芯片仍显示多行，各芯片的设备内存独立参与排序，CPU 和主机内存不重复累加。排序仅作用于交互界面，不改变设备顺序或 JSON 快照的原始顺序。

选中进程行使用反显，其关联的所有 NPU 条目保留原有颜色，其他 NPU 的文字和进度条保持原色并变淡，保留绿/黄/红所表达的负载含义；边框和整机历史不变。进度条使用前景色块填充（Unicode 为 █，ASCII 为 #），避免反显空格导致背景色无法正常变淡。默认无选中行。刷新和排序后按进程身份保持选择并自动滚动到可见区域；同一进程的某条设备占用行消失时，优先跟踪它仍存在的其他设备行。进程身份使用 PID 和 `/proc` starttime，无法验证身份时不能新建选择，避免 PID 重用后关联错误的进程。上下键、Home/End 和翻页会跳过无法验证身份的行。Tab 切换到设备焦点后，仍可独立滚动设备列表。

进程详情提供 `NPU Memory`、`CPU Usage`、`Host Memory (RSS)` 三张历史图，显示实时占用，以 `Y-axis` 标注各图的纵坐标范围。`NPU Memory` 合计该进程在各芯片的设备内存；CPU 和主机 RSS 只取一份，不按设备行重复累加。`Host Memory (RSS)` 同时显示 RSS 容量和占主机内存的比例。CPU 以单逻辑 CPU 满载为 100%，图表量程可随多线程使用率扩展到大于 100%；容量图按字节自动缩放，不混用容量与利用率。

历史从选中该进程时开始积累，使用现有采样线程，将每轮已完成的快照直接入图，保留最近 600 秒；打开帮助或详情不会中断采样，返回列表后再次进入同一目标保留历史，切换到其他进程则清空。绘图时对相邻有效快照之间的短间隙作线性插值，缺失读数和较长采集中断仍留空。某些设备的进程列表或该进程设备内存不可用时，NPU 合计显示 N/A，避免将部分合计当成完整值。目标退出、释放所有 NPU 或 PID 被重用后，详情保留已有历史、将当前值标为不可用，不跟踪到新进程。详情页 `Esc` / `q` / `Enter` 均返回列表；主界面 `q` 仍退出。

图表默认使用 Unicode 盲文点阵（每格 2×4 点），不再使用星号。启动时按当前 locale 初始化；UTF-8 不可用、locale 无效或盲文字符宽度不是单格时自动使用 ASCII 图形。`--ascii` 强制使用 `.` / `:` 曲线和 `+ - |` 边框，帮助页显示当前模式。字体缺字无法自动判断，出现方框时可手动切换。主机内存从上沿向下填充，Swap 从下沿向上填充；零值为基线点，缺失值留空。

已有构建目录若缓存了普通 ncurses 库，重新配置时使用 `cmake -U 'CURSES_*' -S . -B build-top` 清除缓存，保留原有构建选项。

主机、NPU 和进程历史都直接记录每轮已完成的快照，不再按窗口平均。每个盲文小点或 ASCII 格代表启动时 `--interval` 秒的时间尺度，运行中调整等待不会改变此尺度或清空历史；快照按实际采集完成时间映射到固定的时间格，已完成的旧点只按整格左移，插值不会随新快照重新计算。进程详情的纵轴会按历史最大值自动缩放，因此有更高峰值出现时，旧点的屏幕高度仍可能改变。每个历史点记录该轮采集前的等待设置 `wait`，仅在相邻有效快照的间隔不超过 `wait + max(2 秒, wait / 2)` 时，于绘图阶段线性连接中间的空格，不改变原始读数及峰值。缺失读数和较长采集中断仍留空，不无限延续旧值。实时数值来自最新快照。图表宽度决定显示范围，保留最近 600 秒，缩放不清空历史。刻度固定为 30/60/120/180…秒，宽度不足时省略。完整历史区采用上方 5 行、时间轴 1 行、下方 5 行；主机内存占下方前 4 行，Swap 位于最底行，文字覆盖对应绘图区。矮窗口压缩图表高度。仅发生采集错误时在底部显示错误信息，超过预期采集等待与耗时后标记 STALE。

## 指标口径

- **NPU**：物理 NPU ID，按此排序；另外展示逻辑 ID 和 Card/Chip，映射失败不会拿其他编号代替。
- **Share**：指定芯片的容器共享使能状态，使用 `dcmi_get_device_share_enable` 查询；`On` 表示启用，`Off` 表示禁用，接口缺失、不支持、查询失败或返回异常值时为 `N/A`。它表示功能开关，不表示当前有多少容器共享。首次采集读取，之后每芯片每 5 秒刷新，期间复用缓存；失败后清除旧值并按同样周期重试。JSON 字段 `device_share` 为 `true` / `false` / `null`。详细设备布局保留 Logic ID，Card/Chip 后显示 Share；不足 100 列时 Card/Chip 缩写为 `3/0`，保留完整状态。
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

核心测试覆盖容量加权、缺失值与零值、历史保留、主机 CPU、复杂进程名称、跨设备 PID、PID 重用和退出。`test_layout` 还覆盖 24～100 行、90/120/200 列、0/1/3/8/16/256 芯片下的最低进程空间与视口边界。真实 NPU 主机还可运行下面的 PTY 验收脚本；Python 仅用于测试，不是程序依赖。脚本的 8 芯片、310P3、版本断言对应本次验收主机。

紧凑布局 PTY 覆盖 0/1/8/16 芯片、Auto/Compact、24/28/36/55 行、设备翻页、选择保持、搜索输入隔离、暂停和终端恢复：

```sh
python3 tests/top/pty_compact.py ./build-top/test_process_view_tui --output /tmp/compact-fixture
python3 tests/top/pty_compact.py ./build-top/ascend-monitor-tui --hardware --output /tmp/compact-hardware
```

```sh
python3 tests/top/pty_smoke.py ./build-top/ascend-monitor-tui --output /tmp/ascend-monitor-tui-evidence
```

排序核心测试 `test_process_sort` 随 CTest 运行，覆盖三列全表排序、零值与缺失值、跨芯片 PID、同值顺序及表头点击边界。另可运行不依赖 NPU 读数的鼠标与键盘交互验收，覆盖 Unicode / ASCII、90/120/200 列、帮助页和缩放后的表头位置：

```sh
python3 tests/top/pty_sort.py ./build-top/ascend-monitor-tui
```

搜索核心测试 `test_process_search` 随 CTest 运行，覆盖命令行、用户名、PID、大小写、UTF-8 和跨芯片匹配。搜索交互验收使用确定性数据，覆盖实时高亮、输入与快捷键隔离、无匹配、清除、排序、刷新及缩放：

```sh
python3 tests/top/pty_search.py ./build-top/test_process_view_tui
```

进程选择与详情的核心测试 `test_process_view` 随 CTest 运行。独立测试程序提供确定性数据，可验证跨芯片联动、灰色样式、CPU 大于 100%、缺测和 PID 重用；它只属于测试构建，不会安装到系统，也不会操作真实进程：

```sh
python3 tests/top/pty_process_view.py ./build-top/test_process_view_tui --fixture --output /tmp/process-view-fixture
```

真实 NPU 主机可使用同一脚本验证选择、置灰、详情与缩放，不触发退出/重用模拟：

```sh
python3 tests/top/pty_process_view.py ./build-top/ascend-monitor-tui --output /tmp/process-view-hardware
```

设备指标颜色与标签的 PTY 验收覆盖内存与 AI Core 独立着色、零值与缺失值、50% / 80% 阈值、进程选择淡化，以及 90/100/120/200 列布局。确定性数据用于验证高低负载组合，真实设备模式核对当前数值与进度条的颜色一致性：

```sh
python3 tests/top/pty_device_metrics.py ./build-top/test_process_view_tui --output /tmp/device-metrics-fixture
python3 tests/top/pty_device_metrics.py ./build-top/ascend-monitor-tui --hardware --output /tmp/device-metrics-hardware
```

帮助布局的独立 PTY 验收覆盖页签与鼠标点击、对齐和样式、长文本换行、90×24 滚动、宽屏居中、过小窗口恢复以及关闭帮助返回原界面：

```sh
python3 tests/top/pty_help.py ./build-top/test_process_view_tui --output /tmp/help-layout
```

动态间隔与画面暂停的独立 PTY 验收覆盖每次 0.1 秒、上下限、自定义小数初值、等待中唤醒、暂停时后台采样、冻结主表/详情/历史、恢复最新数据、搜索输入隔离、窗口缩放及终端恢复：

```sh
python3 tests/top/pty_live_controls.py ./build-top/test_process_view_tui --output /tmp/tui-live-controls
python3 tests/top/pty_live_controls.py ./build-top/ascend-monitor-tui --hardware --output /tmp/tui-live-controls-hardware
```
