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
## 语音插话（实验功能，单台部分验收）

用户已确认当前恢复后的 realtime 版本不再自行打断，播放音量 **60% 时可以正常插话并使用；80% 时需要明显提高说话音量**。60% 是单台实测建议，未设强制上限或改写已有音量。短词和句首识别仍有错误，长期稳定性未通过；已有 alpha.2 Release 不包含本项改动。使用步骤、识别样例和重启问题见[插话说明与实测限制](../../../../docs/stackchan-voice-interruption.md)。

本轮仍出现 AEC 调度看门狗告警，以及 AXP2101 电池状态读取超时触发 abort 的重启；电池读取容错尚未修复。下面 FT6336 的可恢复读取只覆盖触摸轮询，不代表电池读取也已具备容错。

K151 构建默认启用设备端 AEC 和现有 `realtime` 对话模式，连续上传经过 AFE/AEC 处理的音频，由小智服务端处理插话。之前额外叠加的 360 毫秒连续人声确认、PCM 延迟缓存和静音替换已撤除，避免拦截短句和带停顿的语音。已有 VAD 继续报告人声状态，但不再改变上传数据。当前使用 WebRTC VAD，尚未切换 VADNet，也未增加本地 VAD 自动 abort。每秒只记录帧数、VAD 状态统计与处理后峰值，不保存音频。官方 `vad_cache` 是分段上传补回句首的机制；连续上传不能重复补入同一段音频。[Espressif VAD 文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/vadnet/README.html)。

FT6336 触摸轮询已移到独立的低优先级任务；20 毫秒定时器只发送任务通知，不在回调中同步读取 I2C。任务合并迟到通知，避免补跑旧轮询；读取失败仍跳过样本并取消未完成的短触识别。该变更依据[ESP Timer 回调要求](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/esp_timer.html)。它消除了触摸读取阻塞共享定时器的调用路径，不代表已消除 AEC 负载引发的全部 CPU 0 看门狗警告。插话可靠性、误触发率、负载与原有交互仍须实机验收。

CoreS3 官方原理图的 ES7210 MIC1 接麦克风，MIC3 接扬声器的电气回声参考。TDM I2S 的线上顺序是 MIC1、MIC3、MIC2、MIC4，接收掩码按时隙编号，增益接口按 ADC 编号，两者不可混用。启用 AEC 时使用四时隙 TDM、选择时隙 0 和 1（掩码 0x3），向 AFE 提供 `MR` 顺序的两路 PCM；MIC3 增益则通过 ADC 索引 2 设为 0 dB。RX/TX 共用 64fs 时钟，因此扬声器使用两时隙、32 位格式，由 AW88298 驱动同时设置位宽和 BCK 比例。设备内部的 16 位 PCM 经有界分块转换为高位对齐的有符号 32 位样本，不改变音量，不在播放路径分配堆内存。关闭 AEC 时保持原有 16 位播放路径。

初次试验选择时隙 0 和 2（掩码 0x5），用户报告无法收音；随后改为四时隙、掩码 0x3，用户确认收音恢复但扬声器无声。这两份试验固件均未通过验收。同步为 32 位播放后用户确认收音和声音恢复，但报告插话过于灵敏，完整日志还发现 FT6336 触摸芯片读取超时触发 ESP_ERROR_CHECK 导致整机重启。因此该版本仍未通过验收。新版本为运行中的触摸轮询改用可恢复的读取结果：超时跳过本次样本并取消未完成的短触识别，避免使用旧数据或合成触摸释放；运行时触摸读取错误不再主动退出程序。触摸交互调度回应用主任务。此处理避免已确认的致命错误路径，不等同于证明 I2C 超时的底层原因已消除。仅缩回两时隙的方案因不能保证正确的参考通道而放弃，未烧录到设备。参见[原理图第 4 页](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf)、[ES7210 数据手册第 8 页](https://files.waveshare.com/wiki/common/ES7210_DS.pdf)、[Espressif 通道映射实现](https://github.com/espressif/esp-audio-dev/blob/main/esp_codec_dev/device/es7210/es7210.c)与[ESP-SR AEC 文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html)。

需要对真实设备检查：收音、播放、唤醒；机器人讲话中直接插话；不插话时是否自己打断自己；连续插话；断网重连；原有表情和运动功能。回声参考接线、输入增益和扬声器音量都会影响结果。当前 AFE 的 FD_LOW_COST / VERYAGGR 参数保持原值，后续依据实测调整。

如需回退到等说完再收音，可在标准构建命令后追加 `--build-options-json '{"aec_mode":"off"}'`，此时禁用设备端 AEC 和参考输入；不会改动个人绑定与头部校准。

## 历史开发与诊断记录

[2026-10-01 的实验、回退与断电复测](../../../../docs/stackchan-history/MOTION-20261001.md)。串口错码根因尚未确定，短时恢复不能证明长期稳定。

### 回声链路诊断（未验收）

当前开发版新增 `RawDuplex` 数值日志，在 `esp_codec_dev_read` 返回后、重采样前统计原始 24 kHz MR 输入：每秒样本数、两路 RMS、达到 int16 上下限的样本数、播放音量和麦克风增益。`DuplexInput` 则报告重采样到 16 kHz 后的满幅样本数，用于区分采集/格式问题和重采样阶段的变化。达到满幅只表示采样触及边界，不能单独证明模拟 ADC 削波或其原因。

原始输入另以 1/8 抽样统计 Pearson 相关系数，比较参考领先麦克风 0、24、48、120、240 个样本（24 kHz 下 0、1、2、5、10 ms），输出绝对相关性最大的候选及带符号的千分值。仅使用 241 个参考样本的易失环形窗口，不保存录音。这个粗略数值不是完整的延迟估计、ERLE 或验收标准；低相关可能来自错路、时序、噪声或非线性失真，需要安静播放对照。

依据 [Espressif 音频 FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/audio-development-framework.html)，先验证录放音与有效参考信号，再调整 AEC。上游 CoreS3 的 `AUDIO_INPUT_REFERENCE=false` 与原厂 StackChan 的 `true` 不同，不能把板型支持等同于全双工插话已验证。本次诊断不改变采样映射、增益、AEC 或 VAD 参数。

### 撤回本地 VAD 自动停播试验

先停播再收音试验改用了本地 120 ms VAD 触发和云端 auto 收句，超出了只丢弃触发前音频的需求。实机出现自身声音误触发，以及实际播放开始前上一句话的 VAD 状态触发。该试验未通过验收，相关策略、队列与 TTS 防护改动均撤回，恢复此前的云端 realtime 持续输入方式。保留原始收音数值诊断与摸头独立任务。仍不能宣称此前 AEC 的自身回声或识别准确率问题已解决。
