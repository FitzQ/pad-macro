# pad-macro Overlay

这是 `pad-macro` 项目的 Overlay 部分（基于 libtesla），用于在系统级叠加界面中为用户提供按键宏的配置与管理界面。

主要功能
- 编辑/查看 `sdmc:/config/pad-macro/config.ini`。
- 添加/删除/选择宏映射（将已录制的宏文件绑定到按键组合）。
- 启动 / 终止目标 program，并在配置变更后通知 sysmodule 重新加载设置。

配置与文件路径
- 配置文件（overlay 负责读写）：
	- `sdmc:/config/pad-macro/config.ini`
	- 格式示例：
		```ini
		[pad]
		recorder_enable=true
		player_enable=true
		recorder_btn=0x30
		play_latest_btn=0x40

		[macros]
		0x30=/config/pad-macro/macros/example.bin
		```
- 宏文件目录：
	- `sdmc:/config/pad-macro/macros/`
	- 录制时会先写入 `latest.bin`，确认后复制为时间戳文件。

构建说明
1. 环境
	 - devkitPro / devkitA64 + libnx
	 - 推荐使用 MSYS2 / devkitPro 的构建 shell

2. 常用命令
```sh
# 构建 overlay + sysmodule（默认日志为精简模式）
make

# 仅构建 overlay
cd overlay && make
```

开启完整日志（调试时使用）
- overlay 的日志系统支持用编译宏开启完整日志（包含文件写入和 UDP netlog）以便调试；默认关闭以节省内存。
- 启用方式：
```sh
make CFLAGS+=" -DENABLE_FULL_LOG=1"
# 或仅对 overlay：
cd overlay && make CFLAGS+=" -DENABLE_FULL_LOG=1"
```

运行与注意事项
- 保存配置时，overlay 会把内容写入 SD 并调用 IPC 通知 sysmodule：确保文件句柄已关闭后再通知，避免 sysmodule 读取到不完整内容。
- 如果需要等待目标 program 启动并注册服务，overlay 中的 `smGetService` 有可能失败；建议增加短重试或使用 TIPC 检查服务就绪。
- UI 切换（`frame->setContent(...)`）需确保 `frame` 和要设置的元素已初始化，否则可能崩溃；代码中已加入延迟应用策略（如果需要，我可以进一步硬化）。

调试建议
- 启用完整日志并检查 `sdmc:/atmosphere/logs/pad-macro.log`。
- 若怀疑 IPC 数据被截断或损坏：优先使用“写文件 + 通知”模式（overlay 写文件，sysmodule 从 SD 读取），避免通过 CMIF 发送大型字符串。

贡献
- 欢迎提交 issue 与 PR，描述复现步骤与日志。


