# StackChan 开发指南

[返回项目首页](../README.md)。本指南只介绍本仓库 StackChan K151 变体的源码、资源生成和接口。硬件校准与诊断请另见[板卡说明](../main/boards/m5stack/stackchan-k151/README.md)。

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

构建脚本会修改本地 `sdkconfig` 并生成构建产物，首次构建需要获取组件依赖。上面的命令仅构建，**不烧录设备**。如何新增其他硬件适配，请参考[上游自定义板卡指南（英文）](custom-board.md)。

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

所有口语示例、触摸操作和可用条件见[交互指令列表](../README.md#交互指令列表)。以下是头部工具的参数映射，方便对照实现：

| 交互 | 工具与参数 |
| --- | --- |
| 开始 / 停止跳舞 | `self.robot.dance`：`action=start` / `stop` |
| 回正 / 抬头 / 低头 / 左看 / 右看 | `self.robot.set_head_pose`：`pose=center` / `up` / `down` / `left` / `right` |
| 再抬 / 再低 / 再左 / 再右一点 | `self.robot.adjust_head`：分别使用正 / 负的 `pitch_delta` 或负 / 正的 `yaw_delta`，每轴 −10～10 的整数 |
| 点头 / 摇头 / 停止动作 / 恢复动作 | `self.robot.head_action`：`action=nod` / `shake` / `stop` / `resume` |
| 查询头部状态 | `self.robot.get_head_position`，无参数 |

返回 `accepted` 只代表入队，实际是否到位应查询反馈。停止跳舞会恢复原姿态并关灯；停止动作会保持当前姿态并暂停自动动作。恢复动作需要已完成本地验收和有效反馈，不会重新开始上一次跳舞。

## 测试与参与开发

```sh
# 主机运动、交互、串口协议、恢复、灯效和音频策略测试
bash tools/test_stackchan.sh

# 上游构建脚本测试
python -m unittest discover -s scripts/tests -v
```

主机 C++ 测试脚本需要 Bash、`clang++` 和 `rg`（ripgrep），并使用 AddressSanitizer / UndefinedBehaviorSanitizer；目前在 Mac M4 上验证。测试通过仍需真实设备验收。提交问题和改动前，请阅读[贡献与问题反馈说明](../CONTRIBUTING.md)。
