# K151 测试烧录包安装

[返回首页](../README.md) · [校准与验收](stackchan-calibration.md)

适用范围：本项目的 **K151 / CoreS3 / ESP32-S3 / 16MB Flash / Quad PSRAM / ILI9342C 屏幕**。不同屏幕或内存版本不能仅凭“K151”名称套用本包；先核对硬件。当前安装工具和包仍在验收准备中，尚未发布通过实机验收的二进制 Release。

安装包中的 `firmware/` 是五个独立镜像，`manifest.json` 记录版本、地址、大小和 SHA-256，`SHA256SUMS` 可核对下载；`licenses/` 和来源说明须随包保留。它不含用户 NVS、账号、Wi-Fi 或任何设备的整机备份。需要 Python 3.10 及以上，不需要 ESP-IDF、Docker 或自行编译。

## 准备与备份

安装过程会替换当前应用。先在原厂 App 按官方转换平台要求解绑（从原厂切换时），然后保留本机原厂备份，确认知道如何恢复。不要按网上示例擦除整个 Flash。USB-C 数据线接底座、底座开关 ON，机器人放稳，头部无遮挡、线缆不妨碍运动。

在解压后的安装包目录执行：

```sh
python3 -m venv .venv
# macOS/Linux
source .venv/bin/activate
# Windows PowerShell 使用：.venv\Scripts\Activate.ps1
python -m pip install -r requirements-install.txt
python tools/stackchan_install.py backup --port /dev/cu.usbmodemXXXX --output backups/before-install
```

替换串口路径。Windows 类似 `COM5`，Linux 类似 `/dev/ttyACM0`。备份会进入下载模式、上传临时 RAM 读写工具并读取 16MiB Flash；不写入固件、不擦除 Flash。结束后设备留在下载模式。如果只是备份，短按 RST 正常启动；准备立即安装可保持此状态。

`backups/before-install/flash.bin` 和 `backup.json` **仅在本机保存，不发送到 Issue、聊天或仓库**，完整 Flash 含个人配置。工具验证分区和 NVS CRC，只输出两个舵机零点，不输出其他 NVS 值。不支持加密、安全启动、其他容量、不完整备份或存在冲突的校准记录；失败时停止，不猜零点。若 NVS 完整有效但完全没有校准记录，显示 `calibration=null`，允许安装，但固件头部保持禁用，必须另取本台可靠记录才能启用。残缺、冲突、越界或 CRC 损坏的记录仍会拒绝安装。

## 首次切换

```sh
python tools/stackchan_install.py plan --package . --backup backups/before-install --mode first-install
python tools/stackchan_install.py install --package . --backup backups/before-install --mode first-install --port /dev/cu.usbmodemXXXX --yes
```

先看 `plan` 的结果。首次安装写入启动程序 `0x0`、分区表 `0x8000`、OTA 选择区 `0xd000`、应用 `0x20000`、自定义表情 `0x800000`。保留 `0x9000..0xcfff` NVS 和 `phy_init`；旧系统必须有完全相同的 NVS 映射，否则工具拒绝。旧应用和资源被替换，不能靠切回旧 OTA 分区回到原厂。

工具核对连接设备与备份身份，并在写入前再次完整读取 Flash，比对备份 SHA-256（包含应用、资源和元数据）；此步骤需要额外等候。启动后可能改变配置，若提示备份过期，应重新备份到新目录。`--yes` 明确接受已显示的写入计划；未加该参数不会连接设备进行安装。镜像逐项校验，写完再次确认 NVS 逐字节未改变。整个过程没有 `erase_flash`；写入目标区域时芯片需要按扇区擦写这些区域。

成功后短按 RST；如果自动复位不可靠，必须手动短按，不能长按。检查屏幕是否正确显示、配网/绑定是否正常，再按照下述本地校准流程启用头部。

本 K151 自定义版本禁止云端固件升级，包括自动更新和远程升级请求，防止云端原厂镜像覆盖本项目。小智激活、服务地址获取与正常对话仍保留；后续固件更新使用经校验的 USB 安装包。不要把版本号调高来绕过更新，也不要对其他板卡套用本包。

## 已使用本项目后的升级

```sh
python tools/stackchan_install.py backup --port /dev/cu.usbmodemXXXX --output backups/before-upgrade
python tools/stackchan_install.py plan --package . --backup backups/before-upgrade --mode upgrade
python tools/stackchan_install.py install --package . --backup backups/before-upgrade --mode upgrade --port /dev/cu.usbmodemXXXX --yes
```

升级要求与本包完全相同的目标分区布局。工具根据经 CRC 验证的 OTA 元数据选择启动分区，只更新该分区应用和表情；保留 bootloader、分区表、otadata、NVS。启动验收尚未完成、损坏或无法确定的 OTA 状态会拒绝升级。首次从原厂或其他小智版本切换，应使用首次安装流程，不要强行套用升级地址。

## 本台校准和运动验收

备份含校准记录时，工具显示本台 `yaw_zero`、`pitch_zero`，安装结束显示相应的 `head calibrate ...` 命令。如果当前系统未保存零点，必须从这台机器自己的可靠原厂备份或校准记录取得，保持头部禁用直到本地配置和验收；不能用其他机器的备份填补。不要照抄他人的数值，也不要把当前姿态的 raw 读数当作零点。

用 115200 波特率串口终端查看 `head status`。如果缺少有效配置，输入工具输出的本台 `head calibrate ...`，短按 RST，然后重新连接。读取 `head calibration`、`head status`，确认配置已载入、反馈正常。按照[完整校准指南](stackchan-calibration.md)执行 `head arm` 和两个 2° 小幅探测、回正及实际方向/范围检查；全部由人确认后在静止状态输入 `head approve`。

提取记录的 `approved=false` 表示工具不会替任何机器授予验收资格。设备 NVS 原本已有本台有效的新校准记录时，安装器保留它；旧版 `head_ctl/verified` 不会被新程序视为验收。不要克隆 NVS 到其他机器。

看到通信超时、过载、姿态不符或机械卡住时，使用 `head stop`，停止继续尝试动作并排查。按[实机清单](stackchan-hardware-validation.md)确认待机、摸头/松手恢复、点头/摇头、跳舞 RGB 和结束关灯。编译及写入校验成功不能替代这些机械验收。

## 恢复

保留原始完整备份。不要把发布包当作原厂恢复固件：它不包含原厂系统和个人配置。恢复整机备份会覆盖应用及 NVS，涉及设备绑定与校准，只能使用这台机器自身的备份，并应单独核对恢复方案；本安装器不提供自动整机恢复或擦除指令。
