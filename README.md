# StackChan 小智机器人

让 M5Stack StackChan 使用小智语音对话，同时保留机器人可爱的表情和头部动作。

本项目基于 [XiaoZhi ESP32](https://github.com/78/xiaozhi-esp32) v2.5.0，针对 **StackChan K151 / CoreS3 / ESP32-S3** 增加独立板卡实现。头部运动参考 [M5Stack StackChan 开源代码](https://github.com/m5stack/StackChan)的驱动和弹簧动画。这是个人开发项目，与 M5Stack、小智官方没有隶属关系。

> **当前是开发源码，尚未提供面向所有设备的通用烧录包。** 舵机零点和 MAC 启用检查来自一台已验收设备，换一台机器人需要重新核对校准、方向、限位和显示屏。直接编译不能替代这些检查；不要简单删除身份检查后启用自动运动。

## 已实现的功能

| 功能 | 当前行为 |
| --- | --- |
| 小智语音对话 | 保留现有小智连接、配网、音频和设备端 MCP 能力；对话需要兼容的小智服务 |
| 原厂风格表情 | 黑色背景，参考原厂几何风格重新绘制眼睛和嘴巴；不是提取原厂 GIF |
| 讲话动嘴 | 21 种情绪，共 41 个 GIF 状态；按实际语音播放切换同一情绪的讲话版本，困困始终闭嘴，普通待机嘴巴不动 |
| 隐藏对话文字 | 表情资源设置隐藏对话字幕，保留其他状态界面 |
| 语音控制头部 | 回正、抬头、低头、左看、右看、点头、摇头、停止与恢复；左右以机器人自身视角为准 |
| 讲话微动作 | 讲话时少量轻微转头或点头，明确动作指令可以抢占 |
| 待机张望 | 仅待机时每约 4～8 秒随机看看周围，进入对话即停止张望 |
| 摸头回应 | 喜欢表情、柔和抬头和持续轻点头；松手约 3 秒后恢复触碰前姿态，新指令可以取消恢复 |
| 猫咪呼噜 | 摸头时本地播放呼噜录音，讲话、聆听等音频任务优先 |
| 跳舞与 RGB | 约 15 秒左右、上下随机摆头，底座 12 颗 RGB 灯切换颜色；结束恢复原姿态并关灯 |
| 保护与诊断 | 反馈读取、范围限制、停滞和过载保护，以及分级通信恢复、UART 和舵机统计 |

跳舞目前是预设时序动作，**不包含放歌、音乐平台接入或节拍同步**。早期内网音乐方案已取消。本项目也不包含小智后端部署和自定义云端音色服务。

## 适配范围与当前状态

- 实测硬件：StackChan K151 原有头部、底座、反馈舵机和触摸传感器，CoreS3、16 MB Flash、8 MB Quad PSRAM，ILI9342C 显示配置。其他屏幕修订版尚未验证。
- 独立板卡目录为 `m5stack/stackchan-k151`，构建变体为 `m5stack-stackchan-k151`。普通 CoreS3 包不能代替这个变体。
- 当前代码中的舵机零点为单台设备校准值，MAC 不匹配时不会启动舵机控制。首次未验收设备不自动启用扭矩；本地验收标志与有效反馈决定后续是否允许自动动作。详见[板卡说明](main/boards/m5stack/stackchan-k151/README.md)。
- 表情资源的基础输入 `assets.bin` **不在仓库中**；应用构建与资源生成是独立步骤。
- 舵机偶发串口错码的根因仍未确定，不能宣称长期完全修复。通信超时、过载与机械停滞是不同状态，不能把所有故障都判断成“头卡住”。

### 已完成的验证

| 日期 | 验证结果 | 结论范围 |
| --- | --- | --- |
| 2026-10-01 | 完整断电后，同一会话内静止采样 60.020 秒、一次约 15 秒跳舞及自动待机复测，无通信错误或锁定故障 | 本轮实机短期检查通过，不能证明长期稳定或确定串口问题根因 |
| 2026-10-06 | Mac M4 ARM64 上主机策略/协议测试、81 项 Python 构建脚本测试与 ESP-IDF 6.0.1 板卡编译通过 | 源码测试和构建验证；没有追加实机烧录或长期运行测试 |

完整记录见[板卡说明中的历史诊断记录](main/boards/m5stack/stackchan-k151/README.md#历史开发与诊断记录)。

## 获取源码与构建

当前默认分支为 `stackchan-head-control`。

```sh
git clone --branch stackchan-head-control https://github.com/a237821375/stackchan-xiaozhi.git
cd stackchan-xiaozhi
```

需要 Git、Python 3 和 ESP-IDF 工具链。最低支持 **ESP-IDF 6.0.1**，推荐 6.1；本项目已实测 6.0.1，IDF 5.x 不适用。安装方式请参考 [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)。Apple Silicon 使用原生 ARM64 工具链即可，不需要额外引入 Rosetta。

先激活已安装的 SDK，下方 `/path/to/esp-idf` 需要替换为实际路径：

```sh
source /path/to/esp-idf/export.sh
idf.py --version

python scripts/build.py m5stack/stackchan-k151 \
  --name m5stack-stackchan-k151 \
  --language zh-CN \
  --wake-word nihaoxiaozhi
```

构建脚本会修改本地 `sdkconfig` 并生成构建产物，首次构建需要获取组件依赖。上面的命令仅构建，**不烧录设备**。如何新增其他硬件适配，请参考[上游自定义板卡指南（英文）](docs/custom-board.md)。

### 表情资源

资源生成入口是 `tools/build_stackchan_assets.py`，绘制和打包源码在 `tools/stackchan-face-sources/`。先将与当前固件匹配、包含字体和唤醒词模型的基础资源文件放到：

```text
tools/stackchan-face-sources/factory-style-talking-20260929/assets.bin
```

该文件必须是资源包，不能使用整机 Flash 或 NVS 备份代替。仓库不提供基础资源下载或通用预打包固件。

在已安装 Pillow 的 Python 环境中运行：

```sh
python tools/build_stackchan_assets.py \
  --source tools/stackchan-face-sources/factory-style-talking-20260929 \
  --output /tmp/stackchan-assets
```

脚本生成 `gifs/`、`assets.bin` 和 `manifest.json`，检查 320×240 尺寸、动嘴状态、资源大小和打包一致性，并保留基础包的字体/唤醒词资源。嘴巴按讲话状态切换，不是音素级唇形同步。

`otto-20260929/prepare_assets.py` 是保留的历史脚本；当前流程只导入它的打包/解包函数，不应直接运行它依赖旧备份目录的入口。

### 部署前核对

先完成本台设备的校准和小幅动作验收，保存当前完整备份，核对显示屏、分区布局和资源包。历史记录里的 `ota_0` 地址只适用于当时那台设备，不能当作所有 K151 的通用烧录地址。这里不提供一条覆盖所有分区的烧录命令；不能用“编译成功”推断可以安全覆盖 NVS、分区表或 bootloader。

## AI 头部控制接口

设备通过现有小智 MCP 注册以下工具，不需要额外部署电脑代理控制舵机：

| 工具 | 参数与用途 |
| --- | --- |
| `self.robot.get_head_position` | 无参数；读取反馈、目标、动作和故障状态 |
| `self.robot.set_head_pose` | `pose`：`center`、`up`、`down`、`left`、`right` |
| `self.robot.adjust_head` | `yaw_delta`、`pitch_delta`：各 −10～10 的整数，设备再次检查范围 |
| `self.robot.head_action` | `action`：`nod`、`shake`、`stop`、`resume` |
| `self.robot.dance` | `action`：`start`、`stop` |

可尝试说“回正”“再抬一点”“点点头”“跳个舞”“停止跳舞”。模型是否正确调用工具需要实际连上服务后验证。返回 `accepted` 只代表入队，实际是否到位应查询反馈。故障锁定时，单纯等待不会自动解除，也不应反复发出恢复指令。

## 测试与参与开发

```sh
# 主机运动、交互、串口协议、恢复、灯效和音频策略测试
bash tools/test_stackchan.sh

# 上游构建脚本测试
python -m unittest discover -s scripts/tests -v
```

主机 C++ 测试脚本需要 Bash、`clang++` 和 `rg`（ripgrep），并使用 AddressSanitizer / UndefinedBehaviorSanitizer；目前在 Mac M4 上验证。测试通过仍需真实设备验收。提交问题和改动前，请阅读[贡献与问题反馈说明](CONTRIBUTING.md)。

## 目录导航

| 路径 | 内容 |
| --- | --- |
| `main/boards/m5stack/stackchan-k151/` | 板卡、显示、舵机、摸头、呼噜和诊断实现，以及详细说明 |
| `tests/stackchan_*`、`tools/test_stackchan.sh` | StackChan 主机测试与执行入口 |
| `tools/build_stackchan_assets.py` | 当前表情资源生成入口 |
| `tools/stackchan-face-sources/` | 表情绘制和资源打包历史源码 |
| `docs/stackchan-history/` | 历史交接资料，包含开发当时的环境和本机路径；不是当前操作指令 |
| `docs/superpowers/` | 历史设计、实施计划和已取消方案；当前行为以本首页和板卡说明为准 |
| `README_zh.md` | 保留的上游小智中文介绍，部分能力与本板卡不同 |

## 来源与许可

- 本仓库保留上游小智历史与 [MIT 许可证](LICENSE)。
- 使用的原厂 FTServo 和弹簧动画代码保留各自 MIT 许可及固定版本来源，见[原厂代码来源记录](main/boards/m5stack/stackchan-k151/factory_upstream/README.md)。
- 猫咪呼噜录音由 Kerzoven 发布，使用 CC0 1.0；见[音频素材来源与处理说明](main/boards/m5stack/stackchan-k151/PURR_LICENSE.md)。
- 固件备份、NVS、联网原始日志、`.env`、密码和 Token 不属于开源内容，不要提交到仓库。

感谢小智 ESP32 和 M5Stack StackChan 的开源贡献。欢迎针对可复现问题、硬件适配和文档提出 Issue 或 Pull Request。
