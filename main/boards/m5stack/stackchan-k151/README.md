# StackChan K151 / CoreS3 head-control variant

基于 Xiaozhi v2.5.0，使用 CoreS3 原引脚及 ILI9342C、Quad PSRAM。独立板卡标识 `m5stack-stackchan-k151`，避免普通 CoreS3 OTA 包意外覆盖头部控制。

本机校准数据来自设备 `80:45:6b:4d:3a:94` 的原厂备份：yaw zero 461、pitch zero 610；SCSCL big-endian 总线 UART1 TX6/RX7，1 Mbps，ID1/ID2。不是通用舵机固件：MAC 不匹配时不启动舵机控制。没有舵机 EEPROM/永久零点写入。

启动时按原厂流程通过 I2C 0x6f 的 PY32 扩展器 pin 0 开启 VM_EN 舵机电源；对方向、上下拉和输出寄存器逐项读改写及回读，不改其它引脚。等待舵机启动并有限次重试读取限位。首次启动只读取舵机寄存器诊断，不自动回正或使能扭矩。默认活动边界 yaw -30..30°、pitch 5..60°，回正位置 (0,10)；pitch 是原厂坐标，不是以水平 0° 为原点的倾角。需先实测方向和安全范围。

USB 控制台维护命令：`head status` / `head diag`（只读寄存器诊断）；有效反馈后 `head arm` 临时启用；`head probe yaw` / `head probe pitch` 各增量 2°；`head stop`；`head center`；`head nod` / `head shake`；`head left` / `head right`。确认硬件后 `head approve` 仅在新 NVS 命名空间 `head_ctl/verified` 中记录验收标志，后续启动仍先读取反馈，不强制回正。AI 无权执行本地校准验收命令。

AI 工具：`self.robot.get_head_position`、`set_head_pose`、`adjust_head`、`head_action`。入队返回 accepted 并非已到位；状态查询提供反馈和目标。首次硬件未验收时工具会报告未启用。停止保持当前姿态并关闭微动作；恢复需已验收和有效反馈。

讲话微动作每轮最多两次，间隔至少 4.5 秒，默认约 3°。明确点头使用 12°/10° 两次动作，摇头使用 ±18°/±15° 两次往返，每段 400 ms（边界处限幅）。讲话微动作仍为 3°、每段 450 ms。明确姿态指令取消本轮微动作；下一轮围绕新姿态运动。嘴巴由播放状态选择 `<emotion>` / `<emotion>_talk` 素材，sleepy 不切换；这不是音素级唇形同步。资源包需使用 `tools/build_stackchan_assets.py` 生成，约 5.2 MB，保留唤醒词/字体与 hide_subtitle。

构建：激活 ESP-IDF 6.0.1 后运行 `python scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi`。禁止直接运行整包烧录命令覆盖分区/NVS；检查本地部署记录并保留整机备份。

上游测试：`python -m unittest discover -s scripts/tests -v`。本地策略与协议测试：`bash tools/test_stackchan.sh`。

运动链路复用原厂 Servo 的弹簧动画、速度映射和结束补发，并调用原厂 FTServo 的 WritePos(id, raw, 20, 0)。原厂 App MotionDataItem 默认速度为 500；按用户偏好，本版提高为 650（app/lib/model/expression_data.dart，固定版本 1b5765599fba8aaad1811d9a79358ccc7051f5f3），不是 MCP 的默认 150。控制任务以 20 ms 为目标周期，串口工作计入周期，超时后不连续补帧。目标误差不足原厂 8 编码刻度（2.5°）时不判定停滞；超出容差的任一轴连续 1.2 秒无进展或运动中过载会停机并释放扭矩。停止/新指令重置动画。来源和许可详见 factory_upstream/README.md。

待机和摸头：仅小智 idle 状态允许每4～8秒随机张望，速度400、yaw约±20°（偏移动作最大±25°）、pitch5～35°；进入对话即停止该张望，明确指令延后自动张望，stop暂停所有自动动作。Si12T头顶传感器I2C0x68按原厂LOW/LEVEL3配置并回读，独立任务每50ms读取，两次一致触碰触发 loving 表情和抬头18°（仍限位60°）。摸头使用独立速度350：先抬头，1.2秒后围绕抬起位置上下±3°轻点头，每段700ms。持续到松手3秒，再以同样柔和速度恢复首次触碰前的实际姿态；连续抚摸只续时，不累加抬头或重启动作相位。新指令取消恢复，显示恢复为最新对话表情；触摸屏原有短按对话功能不变。

参考原厂固定版本1b5765599fba8aaad1811d9a79358ccc7051f5f3的 hal/hal_head_touch.cpp、hal/drivers/Si12T/Si12T.cpp、stackchan/modifiers/idle_motion.h 和 head_pet.h。移植其硬件参数、随机间隔和松手恢复语义，适配现有GIF和运动策略；本版轻触即可回应，不要求完成原厂滑动手势，现有 loving GIF 替代原厂程序绘制的爱心装饰。

反馈边界采用与到位判定相同的2.5°容差，避免目标5°附近反馈4.69°时锁死；命令范围仍为yaw±30°、pitch5～60°，从实际姿态建立或恢复的策略目标也会限幅。容差之外或过载仍立即停机；通信无效采用下述分级恢复。

运行时舵机反馈、位置和扭矩操作遇到通信校验/超时错误时，等待20ms并清除迟到数据后只重试一次。相同位置和扭矩写入可重复执行；舵机报警不重试。重试失败先暂停运动并冻结策略时钟，连续3组两轴完整、有效反馈后，从实测角度重置弹簧并继续；总恢复期限500ms不会被再次丢包或写入失败延长。恢复期间不发送新位置/使能命令，不把缺失值-1转换为角度，不用旧反馈继续运动。保持最后一个已发送的小步目标；超过期限则锁定故障并尝试释放扭矩。Stop始终取消待恢复动作，暂停时Stop会释放扭矩且恢复后禁止自动使能。确认的报警、超载、越界立即锁定，恢复通信不会清除这些保护。状态区分recovering_communication、communication_timeout、mechanical_stall、overload、servo_alarm、unsafe_feedback_or_configuration，AI不可将通信异常直接描述为机械卡住。

UART适配层将原厂FTServo分段构造的请求缓存为单帧后一次发送，接收使用ESP-IDF缓冲阻塞读取，避免逐字节零tick轮询。原厂包格式、校验和Servo动画保持不变；该传输适配仍需实机验证其对偶发通信错误的改善。
# 跳舞指令（2026-10-01）

设备工具 `self.robot.dance`：`action=start` 运行约 15 秒大幅随机左右/上下动作，
同时控制原厂 12 颗 RGB 灯每约 200 毫秒交替换色；`action=stop` 结束并恢复原姿态、关闭灯光。
摸头、头部动作指令和反馈/RGB 通信故障会中断跳舞。共用原有舵机工作任务，
不改舵机永久参数、绑定或表情资源。动作目标保持 yaw −30～30°、pitch 5～60°，
采用原厂弹簧轨迹，速度 650、每 450 毫秒换目标（初版为速度 500、900 毫秒）。
启动、结束及中断时发送全部 12 颗灯的全黑帧并刷新；即使旧 LED 数量为 0 也不使用
零长度刷新，以免灯珠保持上次颜色。关灯失败保留待重试状态。
USB 调试命令为 `head dance` 和 `head dance stop`；状态包含 `dancing`、`rgb_ready`。
RGB 寄存器参考原厂 `PY32IOExpander_Class`（M5Stack，MIT），按原厂初始化 RGB 数据引脚13（输出、上拉、推挽），逐项读改写和回读保留其它GPIO；随后写 LED RAM 和 LED_CFG。
主机策略与原厂轨迹测试、RGB 保存恢复测试、81 项构建脚本测试及板卡编译已通过。
2026-10-01 已更新 ota_0 并通过写入校验；实机体验尚待手动重启后验收。
更新前应用备份在 `backups/dance-20261001/before-dance-ota0.bin`，新应用为同目录的
`dance-ota0.bin`，附 SHA256；未更新 NVS、分区表、bootloader 或表情资源。
同日速度/关灯修正版 `dance-v2-fast-blackout-ota0.bin` 已仅写 ota_0 并通过校验，
SHA256 `bb122337ed101df319112b64173f4a7e8abad2647bf93bfba3ae5b56eb0a1434`。
主机回归与 40ms 控制周期轨迹模拟通过，实机关灯与速度体验待重启后确认。

同日 v3 补齐原厂 RGB 数据引脚初始化，增加超载时两轴 load/current/raw 日志。主机测试与板卡编译通过。实机 v2 记录在换向时触发 overload，未确认机械阻塞；保护阈值未修改，正在实测定位。

通信诊断补充：RGB 数据引脚修复后用户已确认闪烁变色。一轮15秒跳舞完成；后续有短暂通信恢复提前结束及持续错码触发 communication_timeout。底座断电重启后仍能读到错码，尚不能确认为机械故障。静止总线1ms间隔实验60秒出现19次恢复，未消除问题，已撤回；保护阈值及串口速率未改变。
AI 工具返回值区分临时恢复与锁定故障：fault=true 时不能承诺等一会自动恢复；需供电/连接/遮挡检查及有效反馈后单次 resume，再查询实际状态。已停止自动动作，继续排查物理连接与通信原因。v3/v4实验应用及原应用均保留在 backups/dance-20261001。

v5 `dance-v5-fault-guidance-ota0.bin` 已仅写 ota_0，Hash of data verified。总线1ms实验代码已撤回；RGB引脚初始化与故障提示保留。启动后用 head stop 停止自动动作，等待更换USB线/端口的通信对比；尚未声称通信故障修复。

换USB线/端口后v5静止60秒：11次恢复、0次锁定超时。临时回退加跳舞前备份的相同静止检查仍有回包错码与多次恢复，排除了“仅新跳舞代码才出现静止通信异常”的假设；仍未确定底层原因。

旧版本静止60秒对比结果：15次恢复、0次锁定超时。现已恢复v5应用；写完后的USB校验阶段曾中断，随后单独verify-flash显示digest matched，确认完整。静止测试与回正记录分别保存在 /tmp/stackchan-v5-new-cable-hardware.log 与 /tmp/stackchan-v5-center-check.log（完整日志可能含联网信息，分享前需筛选）。底层错码尚未根除，自动动作暂时停止。

v6 UART 诊断（2026-10-01）：保持原厂 UART1 1 Mbps / 8N1、1024 字节收发缓冲，增加 64 项事件队列，并开启 IDF 默认未开启的帧错误通知。每 5 秒打印 UART_STATS 与两轴 SERVO_STATS；所有实际读写尝试（含重试）分别计数，校验失败不会把旧报警状态算作舵机报警。事件计数是下界；queue_full_observations 仅为队列饱和观察次数，不是丢失事件数。未调整动作速度、幅度、保护阈值或舵机永久参数。

v6 已只写 ota_0，写入 Hash 校验通过。固定姿态采样窗口 5009～65009 ms：ID1 读取 2990 次失败 22 次，ID2 读取 2981 次失败 12 次，合计 34/5971（约 0.57%）；UART 帧错误增加 17，FIFO 溢出、接收环形缓冲满、队列饱和观察、舵机报警均为 0。期间有各 3 次恢复流程中的运行时扭矩写入，未发送自动张望目标。启动前 5 秒另有 37 次帧错误，不计入上述一分钟统计。记录到帧错误说明接收数据存在停止位/帧格式异常，仍不能据此判定具体线缆、电源、舵机或软件时序为根因。

随后执行一次 yaw +2° 探测和回正，两条命令接受，最终反馈 yaw −0.31°、pitch 10.94°，在现有 2.5° 到位容差内；全程没有锁定超时或过载日志。该小幅测试不能证明大幅跳舞稳定。自动动作保持暂停；通信问题尚未修复。应用、SHA256、筛选后的头部日志与结构化结果保存在 backups/dance-20261001/dance-v6-uart-stats-ota0.bin、v6-uart-head-only.log、v6-uart-diagnostic-result.json；原始完整日志未放入共享结果。

v7 原厂传输对照：UART 初始化和事件统计沿用 v6，仅将 readSCS/writeSCS/rFlushSCS/wFlushSCS 委托至未修改的原厂 SCSerial 实现，运动与保护保持不变。trace 的 tx_ok 此时仅代表写入字节数正确，不代表原厂函数忽略返回值的 TX 等待已成功。此实验比较整个传输实现，不能单独归因于某个收发时序细节。源码哈希已与 provenance.json 核对，主机策略/协议测试、81 项构建脚本测试、板卡编译及只读代码复核通过。对照包为 backups/dance-20261001/dance-v7-factory-transfer-ota0.bin（附 SHA256）；首次写入因 Mac 未发现 USB 串口而在打开端口前终止，用户重新连接后只写 ota_0 并通过 Hash 校验。

v7 固定姿态采样窗口 5006～65160 ms（60.154 秒）：ID1 读取 3015 次失败 29 次，ID2 读取 3008 次失败 19 次，合计 48/6023（约 0.80%）；帧错误增加 34，FIFO 溢出、接收环形缓冲满、队列饱和观察和舵机报警均为 0。原厂传输没有消除错码，尚不能据此断言硬件损坏，也不能把单轮误码率差异当作显著性能差异。小幅探测和回正后反馈有效、无锁定故障，yaw −1.25°、pitch 10.94°。已撤回实验，源码/build 恢复 v6，设备重新写入保存的 v6 应用且 Hash 校验通过，绑定和其它分区未更新。筛选日志和结构化结果保存为 backups/dance-20261001/v7-uart-head-only.log、v7-uart-diagnostic-result.json。下一步核对内部舵机串口链路及供电，不能仅因这次对照就建议更换电机。

用户随后确认只做了完整断电重启，没有调整或插紧内部连接。沿用 v6、未再次烧录：第一轮有效统计窗口为 55 秒（启动耗时使墙钟 72 秒不足以覆盖完整一分钟），5496 次读取失败 4 次、帧错误 4 次，无锁定故障；保存 v6-after-power-cycle-short-result.json。第二轮按设备统计时间完成完整 60.020 秒静止采样，6002 次读取无失败、无帧错误。随后同一串口会话执行一轮约 15 秒跳舞；动作及结束后检查窗口 25.040 秒，2498 次读取与 1202 次写入全部成功，无帧错误、超时、过载或故障状态。实际跳舞反馈范围 yaw −23.75～26.25°、pitch 6.25～54.06°；收到 Dance ended: all twelve RGB lights off 日志（指令成功，不代替用户视觉验收）。保存 v6-after-power-cycle-motion-result.json 和对应 head-only 日志。

结束测试后，通过已有 USB 应用重启恢复正常自动策略，随后被动观察约 40 秒，无停止/解锁或配置写入。最后完整统计点 35049 ms 两轴读取各 1706 次、写入 284/257 次，全部成功，UART 帧错误为 0；最终 feedback_valid=true、fault=false、automatic=true，已观察自动张望。结果保存 v6-normal-idle-result.json 与对应 head-only 日志。当前恢复正常待机，不再处于诊断暂停；这只是本轮恢复与短期实测通过，尚未确认底层根因，也未证明长期完全无错码。
