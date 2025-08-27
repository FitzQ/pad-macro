# pad-macro — Sysmodule

这是运行在 Atmosphère / Switch 上的 sysmodule 部分，用于手柄按键宏（macro）录制与播放。

概述
- 功能：在后台监控手柄输入，录制按键序列到文件（binary），并在按键映射触发时回放这些宏。
- 与 overlay（位于 `overlay/`）配合使用：overlay 提供 UI 读取并编辑配置（`config.ini`），保存后通过 IPC 通知本 sysmodule 重新加载配置或播放宏。

关键文件与路径
- 配置文件（由 overlay 编辑）：
  - sdmc:/config/pad-macro/config.ini
  - 格式示例：
    [pad]
    recorder_enable=true
    player_enable=true
    recorder_btn=0x30
    play_latest_btn=0x40
    [macros]
    0x30=/switch/pad-macro/macros/example.bin

- 宏文件目录（录好的二进制宏）：
  - sdmc:/switch/pad-macro/macros/
  - overlay 会生成一个 `latest.bin` 作为临时录制文件，并在确认后复制为时间戳文件放到该目录下。

构建 (Build)
1. 环境要求
   - devkitPro / devkitA64 + libnx（与本仓库一致的工具链）
   - 在 Windows 上推荐使用 MSYS2 提供的 shell（`msys2_shell.cmd -mingw64` 或 devkitPro 提供的构建环境）。

2. 在仓库根（本示例为 `examples/switch/pad-macro`）下运行：

```sh
# 构建 overlay + sysmodule（默认不开启全量日志以节省 sysmodule 内存）
make

# 只构建 sysmodule
cd sysmodule && make
```

开启完整日志（可选）
- 默认为了节省内存，sysmodule/overlay 的日志实现可在编译时禁用为 no-op（见 `overlay/source/util/log.c` 与 `sysmodule/source/util/log.c`）。
- 若要启用文件写入与 UDP 网络日志（用于开发调试），在 make 时传入编译宏：

```sh
# 在 MSYS 下示例：
make CFLAGS+=" -DENABLE_FULL_LOG=1"

# 或仅针对 sysmodule 子目录：
cd sysmodule && make CFLAGS+=" -DENABLE_FULL_LOG=1"
```

说明：启用后会引入 socket 与 stdio，可能显著增加内存/堆占用，请仅在开发或排错时启用。

重要运行时设置
- `sysmodule/source/main.c` 中定义了内存堆大小：
  - `HEAP_SIZE` 在 release 模式下默认被设置为较小值（例如 256KB），在调试时会使用更大的堆（1MB）。
  - 如果遇到内存不足或 malloc 失败，可以在本文件调整 `HEAP_SIZE` 后重新编译。

常见问题与排查
- smGetService 返回失败（例如非 0x0 结果）
  - 确认目标 program 已经运行并且已注册服务名 `padmacro`。
  - overlay 使用 `pmshell` 启动程序，程序启动后可能需要一点时间注册服务；可以在 overlay 中增加重试或延迟。

- 配置保存后 sysmodule 没有生效
  - overlay 的实现会把 `config.ini` 写入 SD 卡后调用 IPC `serviceDispatch` 通知 sysmodule；必须保证文件已关闭/flush 才能被 sysmodule 读取。

- 宏文件复制失败（copy 返回 -2）
  - 通常是因为目标目录不存在；overlay/sysmodule 在写入前应确保父目录存在（`fsFsCreateDirectory`）。

开发提示
- 若在 sysmodule 中使用日志，请优先使用编译选项开关来关闭日志以节省内存（见上文）。
- 宏解析函数可能会修改传入缓冲（使用 `strtok_r` 等），确保传入的是可写且以 `\0` 终止的缓冲区。若通过 IPC 直接传输字符串，sysmodule 必须在堆上复制一份并 NUL-terminate 后交给解析函数。

贡献与联系
- 欢迎提交 issue / PR，描述清楚复现步骤与日志输出。

许可证
- 本仓库遵守原作者声明的许可证（请参阅仓库顶部或各模块内的 LICENSE 文件）。

---
（此 README 旨在快速指导如何构建与调试 sysmodule；



