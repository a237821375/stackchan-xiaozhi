# StackChan K151 / CoreS3 头部控制说明

[返回项目中文首页](../../../../README.md)。本页描述当前源码。旧版本实验已移至[历史记录](../../../../docs/stackchan-history/MOTION-20261001.md)，不能作为当前配置指南。

当前源码版本 **0.1.0-alpha.1**。猫咪呼噜相关代码、录音和测试已移除，摸头表情和动作保留。此版本新增每台设备的本地校准配置；尚未烧录或完成新的实机验收。

基于 Xiaozhi v2.5.0，使用 CoreS3 原引脚及 ILI9342C、Quad PSRAM。独立板卡标识 `m5stack-stackchan-k151`，避免普通 CoreS3 OTA 包意外覆盖头部控制。

舵机使用 SCSCL big-endian 总线 UART1 TX6/RX7，1 Mbps，ID1/ID2。零点从本台设备的 `head_cal/config` 读取，不再在源码中限定一台 MAC 或固定零点。首次缺失配置时不启动运动；配置和验收仅允许本地 USB 控制台执行，见[每台设备的校准指南](../../../../docs/stackchan-calibration.md)。没有舵机 EEPROM/永久零点写入。

启动时按原厂流程通过 I2C 0x6f 的 PY32 扩展器 pin 0 开启 VM_EN 舵机电源；对方向、上下拉和输出寄存器逐项读改写及回读，不改其它引脚。等待舵机启动并有限次重试读取限位。尚无本地验收标志时只读取舵机寄存器诊断，不自动回正或使能扭矩；已验收设备在有效反馈和安全配置检查通过后可启用自动动作，但不强制开机回正。默认活动边界 yaw -30..30°、pitch 5..60°，回正位置 (0,10)；pitch 是原厂坐标，不是以水平 0° 为原点的倾角。需先实测方向和安全范围。

USB 控制台：`head calibration` / `head status` / `head diag` 查询；`head calibrate <yaw_zero> <pitch_zero>` 保存本台零点并清除验收；`head revoke` 撤销验收并要求重启。有效反馈后 `head arm` 临时启用，`head probe yaw` / `head probe pitch` 各增量 2°，实测方向与范围后再 `head approve`。修改配置必须重启，旧 `head_ctl/verified` 不会自动迁移。未经确认不能开启大幅动作。完整步骤见校准指南。

AI 工具：`self.robot.get_head_position`、`self.robot.set_head_pose`、`self.robot.adjust_head`、`self.robot.head_action`、`self.robot.dance`。入队返回 accepted 并非已到位；状态查询提供反馈和目标。首次硬件未验收时工具会报告未启用。停止保持当前姿态并关闭微动作；恢复需已验收和有效反馈。

讲话微动作每轮最多两次，间隔至少 4.5 秒，默认约 3°。明确点头使用 12°/10° 两次动作，摇头使用 ±18°/±15° 两次往返，每段 400 ms（边界处限幅）。讲话微动作仍为 3°、每段 450 ms。明确姿态指令取消本轮微动作；下一轮围绕新姿态运动。嘴巴由播放状态选择 `<emotion>` / `<emotion>_talk` 素材，sleepy 不切换；这不是音素级唇形同步。资源包需使用 `tools/build_stackchan_assets.py` 生成，约 5.2 MB，保留唤醒词/字体与 hide_subtitle。

构建：激活 ESP-IDF 6.0.1 后运行 `python scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi`。禁止直接运行整包烧录命令覆盖分区/NVS；检查本地部署记录并保留整机备份。

上游测试：`python -m unittest discover -s scripts/tests -v`。本地策略与协议测试：`bash tools/test_stackchan.sh`。

运动链路复用原厂 Servo 的弹簧动画、速度映射和结束补发，并调用原厂 FTServo 的 WritePos(id, raw, 20, 0)。原厂 App MotionDataItem 默认速度为 500；按用户偏好，本版提高为 650（app/lib/model/expression_data.dart，固定版本 1b5765599fba8aaad1811d9a79358ccc7051f5f3），不是 MCP 的默认 150。控制任务以 20 ms 为目标周期，串口工作计入周期，超时后不连续补帧。目标误差不足原厂 8 编码刻度（2.5°）时不判定停滞；超出容差的任一轴连续 1.2 秒无进展或运动中过载会停机并释放扭矩。停止/新指令重置动画。来源和许可详见 factory_upstream/README.md。

待机和摸头：仅小智 idle 状态允许每4～8秒随机张望，速度400、yaw约±20°（偏移动作最大±25°）、pitch5～35°；进入对话即停止该张望，明确指令延后自动张望，stop暂停所有自动动作。Si12T头顶传感器I2C0x68按原厂LOW/LEVEL3配置并回读，独立任务每50ms读取，两次一致触碰触发 loving 表情和抬头18°（仍限位60°）。摸头使用独立速度350：先抬头，1.2秒后围绕抬起位置上下±3°轻点头，每段700ms。持续到松手3秒，再以同样柔和速度恢复首次触碰前的实际姿态；连续抚摸只续时，不累加抬头或重启动作相位。新指令取消恢复，显示恢复为最新对话表情；触摸屏原有短按对话功能不变。

参考原厂固定版本1b5765599fba8aaad1811d9a79358ccc7051f5f3的 hal/hal_head_touch.cpp、hal/drivers/Si12T/Si12T.cpp、stackchan/modifiers/idle_motion.h 和 head_pet.h。移植其硬件参数、随机间隔和松手恢复语义，适配现有GIF和运动策略；本版轻触即可回应，不要求完成原厂滑动手势，现有 loving GIF 替代原厂程序绘制的爱心装饰。

反馈边界采用与到位判定相同的2.5°容差，避免目标5°附近反馈4.69°时锁死；命令范围仍为yaw±30°、pitch5～60°，从实际姿态建立或恢复的策略目标也会限幅。容差之外或过载仍立即停机；通信无效采用下述分级恢复。

运行时舵机反馈、位置和扭矩操作遇到通信校验/超时错误时，等待20ms并清除迟到数据后只重试一次。相同位置和扭矩写入可重复执行；舵机报警不重试。重试失败先暂停运动并冻结策略时钟，连续3组两轴完整、有效反馈后，从实测角度重置弹簧并继续；总恢复期限500ms不会被再次丢包或写入失败延长。恢复期间不发送新位置/使能命令，不把缺失值-1转换为角度，不用旧反馈继续运动。保持最后一个已发送的小步目标；超过期限则锁定故障并尝试释放扭矩。Stop始终取消待恢复动作，暂停时Stop会释放扭矩且恢复后禁止自动使能。确认的报警、超载、越界立即锁定，恢复通信不会清除这些保护。状态区分recovering_communication、communication_timeout、mechanical_stall、overload、servo_alarm、unsafe_feedback_or_configuration，AI不可将通信异常直接描述为机械卡住。

UART适配层将原厂FTServo分段构造的请求缓存为单帧后一次发送，接收使用ESP-IDF缓冲阻塞读取，避免逐字节零tick轮询。原厂包格式、校验和Servo动画保持不变；该传输适配仍需实机验证其对偶发通信错误的改善。
## 历史开发与诊断记录

[2026-10-01 的实验、回退与断电复测](../../../../docs/stackchan-history/MOTION-20261001.md)。串口错码根因尚未确定，短时恢复不能证明长期稳定。
