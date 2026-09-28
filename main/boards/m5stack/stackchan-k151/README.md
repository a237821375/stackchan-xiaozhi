# StackChan K151 / CoreS3 head-control variant

基于 Xiaozhi v2.5.0，使用 CoreS3 原引脚及 ILI9342C、Quad PSRAM。独立板卡标识 `m5stack-stackchan-k151`，避免普通 CoreS3 OTA 包意外覆盖头部控制。

本机校准数据来自设备 `80:45:6b:4d:3a:94` 的原厂备份：yaw zero 461、pitch zero 610；SCSCL big-endian 总线 UART1 TX6/RX7，1 Mbps，ID1/ID2。不是通用舵机固件：MAC 不匹配时不启动舵机控制。没有舵机 EEPROM/永久零点写入。

首次启动只读诊断，不自动回正或使能扭矩。默认活动边界 yaw -30..30°、pitch 5..60°，回正位置 (0,10)；pitch 是原厂坐标，不是以水平 0° 为原点的倾角。需先实测方向和安全范围。

USB 控制台维护命令：`head status`；有效反馈后 `head arm` 临时启用；`head probe yaw` / `head probe pitch` 各增量 2°；`head stop`；`head center`。确认硬件后 `head approve` 仅在新 NVS 命名空间 `head_ctl/verified` 中记录验收标志，后续启动仍先读取反馈，不强制回正。AI 无权执行本地校准验收命令。

AI 工具：`self.robot.get_head_position`、`set_head_pose`、`adjust_head`、`head_action`。入队返回 accepted 并非已到位；状态查询提供反馈和目标。首次硬件未验收时工具会报告未启用。停止保持当前姿态并关闭微动作；恢复需已验收和有效反馈。

讲话微动作每轮最多两次，间隔至少 4.5 秒，默认约 3°。明确姿态指令取消本轮微动作；下一轮围绕新姿态运动。嘴巴由播放状态选择 `<emotion>` / `<emotion>_talk` 素材，sleepy 不切换；这不是音素级唇形同步。资源包需使用 `tools/build_stackchan_assets.py` 生成，约 5.2 MB，保留唤醒词/字体与 hide_subtitle。

构建：激活 ESP-IDF 6.0.1 后运行 `python scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi`。禁止直接运行整包烧录命令覆盖分区/NVS；检查本地部署记录并保留整机备份。

上游测试：`python -m unittest discover -s scripts/tests -v`。本地策略与协议测试：`bash tools/test_stackchan.sh`。
