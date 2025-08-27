# pad-macro

按键宏录制与播放（Switch / libnx 示例项目）

本仓库包含两个主要模块：

- `overlay/`：基于 libtesla 的叠加界面，提供配置与交互 UI。
- `sysmodule/`：后台服务，负责实际的按键录制与回放逻辑。

主要功能
- 录制手柄按键序列保存为二进制宏文件。
- 将按键组合映射到宏文件，按下映射时自动回放宏。

快速开始
1. 先准备工具链：devkitPro + devkitA64 + libnx。
2. 在项目根目录运行：

```sh
# 构建 overlay 和 sysmodule
make

# 或者分别构建
cd overlay && make
cd ../sysmodule && make
```

开启完整日志（仅调试时）
```sh
# 启用后会打开文件日志与 UDP netlog，可能显著增加内存占用
make CFLAGS+=" -DENABLE_FULL_LOG=1"
```

文件路径与配置
- Overlay 写入：`sdmc:/config/pad-macro/config.ini`。
- 宏文件目录：`sdmc:/switch/pad-macro/macros/`。录制生成 `latest.bin`，确认后复制为时间戳文件。

示例 config.ini
```ini
[pad]
recorder_enable=true
player_enable=true
recorder_btn=0x30
play_latest_btn=0x40

[macros]
0x30=/switch/pad-macro/macros/20250828-123456.bin
```

常见问题与排查
- smGetService 失败：确认程序已启动并注册服务名 `padmacro`，考虑重试逻辑。
- 配置保存后 sysmodule 未更新：确保文件已 flush/close 后再发送通知。
- 宏文件复制失败（-2）：创建目标父目录。
- IPC 字符串被截断：优先采用文件写入 + sysmodule 从 SD 读取，而不是通过 CMIF 传输大型文本。

贡献
- 欢迎 PR、Issue、测试用例。
