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

开启日志（仅调试时）
```sh
#Makefile中取消以下注释
#CFLAGS	+=	-D__DEBUG__
#将common/log.c中ip设成你的电脑ip
#const char* ip = "192.168.1.3";
#电脑运行以下命令可以接收运行日志
nc -klu 10555
```

文件路径与配置
- Overlay 写入：`sdmc:/config/pad-macro/config.ini`。
- 宏文件目录：`sdmc:/config/pad-macro/macros/`。录制生成 `latest.bin`，确认后复制为时间戳文件。

示例 config.ini
```ini
[pad]
recorder_enable=true
player_enable=true
recorder_btn=0x30
play_latest_btn=0x40

[macros]
0x30=/config/pad-macro/macros/20250828-123456.bin
```

贡献
- 欢迎 PR、Issue、测试用例。
