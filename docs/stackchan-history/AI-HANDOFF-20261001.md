# StackChan 项目与采购交接文档

> 历史归档：以下为 2026-10-01 早期交接快照，不代表当前任务或设备状态。后续跳舞/RGB 与通信诊断、断电后的实测结果见[运动实验归档](MOTION-20261001.md)。当前用法见[本地校准指南](../stackchan-calibration.md)：0.1.0-alpha.1 已取消源码中的单台 MAC/零点限制，旧验收不会自动迁移。原文保留用于了解开发和采购讨论过程。2026-10-06 已移除未在实机实现的呼噜音频代码、录音和专用测试；下文相关路径与转换命令只用于追溯，不再适用于当前版本。

交接日期：2026-10-01，Asia/Shanghai。供接手的 AI 阅读。以下区分实测事实、历史记录和待确认事项；不要把建议视为已实施。

## 1. 用户当前目标与合作方式

当前任务是评估购买移动底盘，让现有 StackChan 能移动：确认尺寸、接口、电源和补购配件。尚未购买确认，尚未开发底盘功能。本次交接没有烧录、运动或修改固件。

之前的固件开发仍有未完成的实机验收，详见第 5 节。不要因为接手就自动烧录或驱动机器人。

用户希望能直接完成终端检查、排查和已授权工作，不要反复询问相同确认。涉及插拔、密码、登录、物理按键才要求用户操作。不要索取密码或复制凭据。用户重视机器人可爱、自然的表达及可靠性，不喜欢机械卡顿。

最初任务仅搭建环境、禁止烧录；之后用户明确改变目标，授权解绑、替换小智固件、增加表情和头部动作。不能把最初的禁烧录要求误当作后续从未获得授权，也不能把过去的授权扩大为任意擦除或更换硬件。

## 2. Mac 环境

2026-10-01 本次实际执行版本命令：

| 项目 | 当前结果 |
| --- | --- |
| 主机 | 用户设备为 Apple M4 Mac；历史环境记录为 Mac mini / Mac16,10 / 16GB RAM |
| macOS | 26.6.2，25G83 |
| 架构 | arm64 |
| Command Line Tools | `/Library/Developer/CommandLineTools` |
| Git | 2.54.0 (Apple Git-157) |
| Python | 3.11.15 |
| Node / npm | v22.22.2 / 10.9.7 |
| Homebrew | 7.0.7，`/opt/homebrew/bin/brew` |
| PlatformIO | Core 6.2.0，`/Users/longteng/.local/bin/pio` |
| Arduino CLI | 1.5.1，`/Users/longteng/.local/bin/arduino-cli` |
| GitHub CLI | 2.101.0，`/Users/longteng/.local/bin/gh` |
| VS Code | 1.139.1，arm64；版本命令伴有 macOS task_name_for_pid 错误提示，但返回版本成功，本次未重新测试 GUI |

初始环境说明：`/Users/longteng/Developer/stackchan/README.md`。它是 9 月 28 日环境准备阶段的记录，其中“未编译/未烧录/未安装 ESP-IDF”等内容后来已过时。

历史记录：Git 姓名 longteng，GitHub 账号 a237821375，提交邮箱为 GitHub 隐私邮箱；gh 已授权，GitHub SSH/HTTPS 访问曾通过。不要改正常工作的 Git 配置，不要读取或输出私钥、token。此次未重新验证登录状态。

VS Code 曾安装 C/C++ 和 PlatformIO IDE。PlatformIO 曾验证 CoreS3 板卡定义及不安装依赖的空项目初始化；Arduino 曾验证官方索引。不能据此宣称 Arduino/PlatformIO 完整固件编译链已验证。

当前实际固件开发路线是 ESP-IDF：

- SDK 路径：`/Users/longteng/Developer/stackchan-sdk/esp-idf-v6.0.1`，由现有 CMakeCache 确认。
- Python 环境：`/Users/longteng/.espressif/python_env/idf6.0_py3.11_env/bin/python`。
- 使用前执行 `source /Users/longteng/Developer/stackchan-sdk/esp-idf-v6.0.1/export.sh`，再确认 `idf.py --version`。
- 仓库 AGENTS.md：建议 6.1，最低 6.0.1，禁止 IDF 5.x；现有本地构建使用 6.0.1。

## 3. 机器人与备份

- M5Stack StackChan，SKU K151，CoreS3 / ESP32-S3。
- 用户提供购买/收货时间：2026-09-27；原厂固件版本 1.5.1。收货日期不能单独证明屏幕硬件版本。
- 历史设备识别：MAC `80:45:6b:4d:3a:94`；16MB Flash，8MB PSRAM。
- 历史串口：`/dev/cu.usbmodem13401`；USB VID:PID `303A:1001`。串口名可能改变。
- **本次枚举只有 Bluetooth-Incoming-Port 和 debug-console，没有机器人 USB 串口。** 不要假设仍连接。
- 原厂 APP 已由用户操作解绑，后来替换为小智固件，曾成功语音对话。当前平台绑定及联网状态未重新检查。
- 保留完整机器人头部/底座机构；USB-C 接底座，避免头部线缆影响转动。
- USB 自动复位历史上多次异常，烧录后常需要用户短按 RST；不要要求长按。

备份目录：`/Users/longteng/Developer/stackchan/backups/`，本次确认存在：

- `stackchan-k151-factory-1.5.1-after-unbind-20260928.bin` 及 `.sha256`
- `xiaozhi-before-factory-style-20260929.bin`
- `xiaozhi-before-head-control-20260929.bin`
- `xiaozhi-before-otto-20260929.bin`

恢复备份前先核对大小、哈希、对应分区和记录。原厂备份是在解绑后制作，不保证恢复后自动恢复云端绑定。不要随意 erase_flash、改分区、bootloader、NVS 或舵机永久零点。

## 4. 代码位置与已经实现的行为

当前工作区：`/Users/longteng/StackChan`。
实际 Git 仓库：`/Users/longteng/StackChan/firmware`，远程 `https://github.com/78/xiaozhi-esp32.git`，分支 `stackchan-head-control`。

实现基于小智 v2.5.0；独立板卡标识 `m5stack-stackchan-k151`，避免普通 CoreS3 OTA 固件覆盖自定义功能。
接手先读 `firmware/AGENTS.md` 和 `firmware/main/boards/m5stack/stackchan-k151/README.md`。

历史实现及用户偏好：

1. 原厂风格的黑底眼睛、嘴巴 GIF。说话时各情绪使用 `_talk` 动画；困困 sleepy 不动嘴；普通待机嘴不动。按音频播放状态切换，不是音素唇形同步。
2. 用户要求不显示对话字幕，资源配置保留 hide_subtitle。
3. AI 可请求回正、抬头、左右转头、点头、摇头和指定姿态；讲话伴随小幅动作。
4. 明确点头/摇头改为多段两次动作，并按用户要求提高速度和幅度，参考原厂 App 的动画与速度模型。
5. 待机每 4～8 秒随机张望，进入对话停止；明确指令延后自动动作。
6. 摸头 loving 表情、柔和抬头、轻点头；松手约 3 秒恢复触碰前姿态。
7. 曾尝试摸头播放低音量本地猫呼噜声。**后续更正（2026-10-06）：用户确认实机未实现，不应视为已完成功能。**素材源为 `https://opengameart.org/sites/default/files/cat_purrsleepy_loop.wav`，许可记录在板卡目录 `PURR_LICENSE.md`，接手复用前读取。

资源目录：`/Users/longteng/Developer/stackchan/assets/`，有 `factory-style-talking-20260929`、`cat-purr`、`head-control-20260929` 等。预览为 `factory-style-talking-20260929/preview.html`。

主要代码均在 `firmware/main/boards/m5stack/stackchan-k151/`：

- `stackchan_k151.cc`：板卡初始化与集成。
- `stackchan_head.cc/.h`：头部控制、串口事务和反馈。
- `motion_policy.h`：待机、摸头、动作策略。
- `feedback_recovery.h`：反馈通信恢复。
- `factory_servo.cc/.h`、`factory_axis.h`、`factory_upstream/`：原厂运动实现与来源。
- `head_touch.h`、`servo_power.h`：触摸与舵机电源。
- `servo_protocol.h`、`buffered_servo_bus.h`、`servo_tx_frame.h`：总线适配。
- `face_state.h`、`stackchan_display.h`：表情。
- `purr_loop.h`、`purr_pcm.h`：本地呼噜音频。

硬件实现记录：SCSCL 总线 UART1 TX GPIO6/RX GPIO7，1Mbps，两轴 ID1/ID2；校准 yaw zero461、pitch zero610，限定本机 MAC 才启动运动。PY32 I²C 0x6f 控制 VM_EN，Si12T I²C 0x68 摸头传感器。不要把这些总线舵机当普通 PWM 舵机。

活动边界 yaw ±30°、pitch5～60°，回正 (0,10)；pitch 为原厂坐标，不是水平倾角。当前值以源码为准。不要为解决故障直接扩大范围或删除保护。

AI 工具：`self.robot.get_head_position`、`set_head_pose`、`adjust_head`、`head_action`。accepted 仅代表入队，不代表到位。本地验收由 `head approve` 写 `head_ctl/verified`，AI 无权自行验收。不要因历史验收提示而盲目重复写 NVS。

## 5. 最重要的未完成项：运动通信恢复实机验收

历史出现：摸头抬头后不再点头、松手不恢复、回不了正；AI 报“头部卡住”。用户曾确认改进后能抬头并轻点头，但后来明确发现 **待机自动动头时恰好摸头容易卡住**。这不是已证实的机械故障，底层有舵机反馈超时/校验错误，根因尚未确定。

此前尝试包括边界容差、帧缓存、阻塞接收、有限重试，以及原厂动画移植。不要把“编译成功/烧录成功”写成“实机已修复”。

最新恢复设计：通信错误先有限重试；失败暂停运动并冻结策略时钟；500ms 内连续3组两轴有效反馈后，从实测角度重置弹簧并继续。恢复期间不发新位置/使能，不把缺失值 -1 当角度。超时锁定故障；真实报警、过载、越界立即停机。Stop 取消待恢复动作。具体行为以当前源码为准。

最新制品：`/Users/longteng/StackChan/artifacts/head-recovery-20260929/xiaozhi-head-recovery.bin`

- 大小 3081520 字节；应用偏移 `0x20000`。
- SHA256 `61bbbf5009fa79f4e4c13178eb87dec08a1155abdc8113f5c914daf9a19b520a`。
- `manifest.json` 记录 app-only 烧录校验成功；`hardware_verified: false`。
- 历史验证记录：7 个本机测试套件和 ESP-IDF6.0.1 构建通过；本次交接未重跑。
- 待验收：待机途中摸头、讲话中断、通信异常自动恢复，以及反复操作后是否仍正常。
- 同目录有 `flash.log`、监控日志、`flash_and_monitor.py` 和 `monitor_boot.py`。检查脚本和当前串口后再使用，不能盲目照搬偏移烧录其他文件。

2026-10-01 Git HEAD 为 `066d57d`。最新恢复实现未提交：

```text
 M main/boards/m5stack/stackchan-k151/README.md
 M main/boards/m5stack/stackchan-k151/motion_policy.h
 M main/boards/m5stack/stackchan-k151/stackchan_head.cc
 M main/boards/m5stack/stackchan-k151/stackchan_head.h
 M tools/test_stackchan.sh
?? main/boards/m5stack/stackchan-k151/feedback_recovery.h
?? tests/feedback_recovery_test.cc
```

不要 reset/clean 丢掉这些改动，不要只 checkout HEAD 后认为拿到了最新修复。

近期提交：066d57d 待机转摸头回归场景；d2e9000 原始故障帧记录；8e080c8 呼噜；a194f2a 整帧发送/阻塞接收；c913b28 有限重试。

常用主机测试（不驱动机器人）：

```sh
cd /Users/longteng/StackChan/firmware
bash tools/test_stackchan.sh
python3 -m unittest discover -s scripts/tests -v
```

历史板卡构建命令（仅构建，不烧录；会改变本地构建配置）：

```sh
source /Users/longteng/Developer/stackchan-sdk/esp-idf-v6.0.1/export.sh
cd /Users/longteng/StackChan/firmware
python scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi
```

资源构建：`tools/build_stackchan_assets.py`，音频转换：`tools/convert_purr.py`，先检查参数和已有产物，不要重复生成覆盖。

## 6. 当前采购：N3 三轮全向底盘

用户提供商品：**塔克创新 N3迷你Omni三轮全向小车底盘，N20电机**。
淘宝商品 ID `916788087255`，SKU ID `5785145840856`。链接：`https://item.taobao.com/item.htm?id=916788087255&skuId=5785145840856`。

根据用户截图，机械套餐包含：3mm 亚克力板×2、N20电机（焊XH2.54线）×3、固定座×3、50mm Omni轮×3、30mm M3铜柱×6、螺丝工具。未列控制板、电池或充电器。标题“麦轮”与清单“三轮Omni”有差异，必须确认实际 SKU。

不能从照片确认底板尺寸、载重、电机额定电压、转速或堵转电流。不要把同品牌四轮或 R20 资料套用到 N3 三轮。

用户又提供 **OpenCTR B60S 控制器**参数截图：

| 参数 | 截图标注（尚未独立实测） |
| --- | --- |
| 主控 | STM32F103RCT6 |
| 输入 | 7～17V |
| 电机 | 4路，XH2.54，AT8236×4，过流保护 |
| 输出 | 5V/5A；另有电源并联输出×1 |
| 舵机 | 6路PWM，不能据此接StackChan内部总线舵机 |
| 尺寸 / 孔距 | 58×68×15mm / 49×58mm |
| 扩展 | UART、SPI、IIC、ADC；标注可连接ESP32等上位板 |
| 下载 | USB-C串口，DTR/RST，SWD |
| 保护 | TVS、3.3V短路、电机过流/过热 |

当前判断：B60S 是合理候选，已含主控和四路驱动；若电流和接口匹配，不必重复买驱动板/ESP32/Arduino。**尚未确认其三轮全向固件和3.3V串口协议。** 四路输出不等于现成支持三轮运动学。

推荐架构：保留整台StackChan固定在底盘上，由StackChan发高层指令，B60S执行轮子控制。未来需要协议和固件开发，现在未实施。

机器人官方尺寸54×70.5×61.5mm、重量187.2g，尺寸顺序不等同已确认安装底面。必须核对底盘顶板图、孔位及机器人加电池总载重，再做固定转接板。

StackChan Grove HY2.0四针与电机XH2.54两线不是相同接口。电机必须接驱动输出，不能直接接GPIO/Grove。板端通信还要确认电平、针序和共地。

电源：裸底盘需另买电池/充电器。7～17V是B60S输入范围，不代表电机允许同样电压；确认驱动是否直接使用输入电压。若共用电池供机器人，使用经确认的稳压5V USB-C输出，不能把电池电压直送机器人5V口。5V/5A标签还不足以证明连接方式、持续电流和电机启动时供电稳定性。

尚待卖家确认：

1. N3三轮Omni的外形、上层板和孔位CAD、建议载重。
2. N20额定电压、RPM、堵转电流、是否有编码器。
3. B60S能否直接配这个电机插头，是否带三轮固件、源码及3.3V TTL串口协议。
4. 配套电池、充电器、电源开关；5V输出能否稳妥给StackChan USB-C供电。
5. 完整套餐价格及是否附通信线、控制板固定件。

截图未体现编码器；不要承诺精确里程、导航或速度闭环。普通移动与自主导航是不同范围。

## 7. 淘宝访问限制与用户提供资料

本次浏览器工具明确返回禁止访问该淘宝网址，并禁止换浏览器/截图/间接执行等绕过。没有提供具体拒绝原因。不能声称已证明是淘宝反爬，或用户登录失败。

用户不满意访问受限，选择交给别的AI。接手AI应按自己的实际工具和权限处理，不要假装本AI已经阅读完整商品页。这里的产品信息主要来自用户截图。

截图文件（本机临时目录，可能被清理，建议用户在新会话重附）：

- 裸底盘清单：`/var/folders/mr/qzkzcx_179s8h17frpm_1vtw0000gn/T/codex-clipboard-a75fcb8d-d8cf-4f58-b77a-7680e60afe7b.png`
- 商品宣传图：`/var/folders/mr/qzkzcx_179s8h17frpm_1vtw0000gn/T/codex-clipboard-bdfe1295-73af-4954-ae7a-ea09a27c00f5.png`
- B60S参数：`/var/folders/mr/qzkzcx_179s8h17frpm_1vtw0000gn/T/codex-clipboard-e45182c4-21c8-44c2-9d6d-809f5bfd4954.png`

## 8. 参考与接手顺序

- StackChan 官方：`https://docs.m5stack.com/en/StackChan`
- 官方结构尺寸：`https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/Model_Size.pdf`
- 原厂开源代码：`https://github.com/m5stack/StackChan`；已移植部分固定版本 `1b5765599fba8aaad1811d9a79358ccc7051f5f3`，来源与许可见 `factory_upstream/README.md`。
- 塔克创新：`https://www.xtark.cn/`；此前没有找到这款N3三轮完整规格。
- 用户曾给小智相关飞书链接：`https://my.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb`，不要假设已获取其内容。

接手先处理当前采购问题，索取或读取确切规格，给出可购买的完整清单。若用户转回固件修复，再读取未提交diff、日志和manifest，确认设备连接、供电与真实反馈，从待机→摸头的重现场景开始验证。采购和舵机通信故障要分开处理。

文档不包含Wi-Fi密码、GitHub token、SSH私钥或云端设备凭据。固件备份可能包含用户配置；不要公开上传。
