# StackChan K151 持续集成与依赖固定

[返回项目首页](../README.md) · [开发指南](stackchan-development.md)

StackChan 专用工作流是 [`.github/workflows/stackchan.yml`](../.github/workflows/stackchan.yml)。它在 `stackchan-head-control` 分支收到 push、目标为该分支的 pull request 和手动触发时运行。上游全板卡工作流 `build.yml` 保留独立的触发条件和 SDK 策略。

## 测试与构建

主机任务在 Ubuntu 24.04、Python 3.11 上安装 `clang`、`ripgrep` 和 [`tools/requirements-stackchan.txt`](../tools/requirements-stackchan.txt)，运行：

```sh
bash tools/test_stackchan.sh
python -m unittest discover -s scripts/tests -v
python -m unittest discover -s tests -p 'test_stackchan_assets.py' -v
```

C++ 主机测试使用 AddressSanitizer 和 UndefinedBehaviorSanitizer，包含运动、交互、串口协议、恢复、讲话状态、灯效及校准逻辑。上游构建脚本当前有 81 项 Python 测试，资产测试单独运行，后续新增测试由 discovery 自动收集。

主机测试通过后，固件任务构建 `m5stack/stackchan-k151` 的 `m5stack-stackchan-k151` 变体，选择 `esp32s3`、中文和 `nihaoxiaozhi` 唤醒词。SDK 使用已经在本项目验证的 **ESP-IDF 6.0.1**，容器固定为官方镜像：

```text
espressif/idf:v6.0.1@sha256:efc19fae2f52fc6873630c668da26aa834139a063b5fb73a46ebc0dbd217b587
```

2026-10-06 已通过 [Docker Hub 官方标签接口](https://hub.docker.com/v2/repositories/espressif/idf/tags/v6.0.1) 核实该 tag 为 active，包含 Linux amd64 和 arm64 镜像。摘要固定了当前多平台镜像清单，避免同名 tag 后续变化影响 CI。镜像使用方式见 [ESP-IDF 官方 Docker 文档](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-guides/tools/idf-docker-image.html)。GitHub Actions 也固定到对应发布版本的完整提交摘要。

## 组件锁文件

[`dependencies.lock`](../dependencies.lock) 纳入 Git，保留本地 ESP-IDF 6.0.1 / ESP32-S3 已成功构建使用的组件版本、组件哈希和 manifest 哈希。锁文件只引用公开 Espressif 组件注册表以及 IDF 来源，不包含个人机器路径、凭据或本地组件覆盖路径。`require: private` 是组件之间的依赖可见性，与私有注册表或访问令牌无关。

`main/idf_component.yml` 继续表达兼容版本范围；`dependencies.lock` 固定这次解析结果。本次流程配置不执行依赖升级，也不更改已经安装的 SDK 或组件。CI 在构建前记录 lock 的 SHA-256，构建后检查字节完全一致；组件管理器如重写 lock，任务会失败，要求维护者先检查依赖变化。

构建任务设置 `IDF_COMPONENT_CHECK_NEW_VERSION=0`，关闭组件管理器用于提示新版本的额外求解和网络查询；锁定版本的组件获取、完整性检查和构建仍正常执行。

需要升级 SDK 或组件时，应在独立改动中重新解析 lock、查看版本及组件哈希差异、运行主机测试并重新构建 K151。此 lock 的目标为 ESP32-S3；不代表其他芯片目标已验证，切换芯片可能需要重新解析对应依赖。

## 匹配当前构建的表情资源

流水线先通过固件构建生成 `build/generated_assets.bin`，再将它作为显式基础输入：

```sh
python scripts/build.py m5stack/stackchan-k151 \
  --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi
python tools/build_stackchan_assets.py \
  --source tools/stackchan-face-sources/factory-style-talking-20260929 \
  --base-assets build/generated_assets.bin \
  --output build/stackchan-assets
```

这样资源包中的字体和唤醒词模型来自本次构建的公开组件，表情绘制来自已纳入仓库的源码，不需要个人机器上的资源包、Flash 或 NVS 备份。脚本检查资源结构、GIF 尺寸和动嘴状态、分区大小、打包一致性，并验证基础字体和模型被保留。Pillow 版本由 `tools/requirements-stackchan.txt` 固定。

## CI 工件和权限

成功运行生成一个保留 14 天的 `stackchan-k151-idf6.0.1-<SHA>` 工件，包含：

- `xiaozhi.bin`：应用固件。
- `stackchan-assets/assets.bin` 与 `manifest.json`：自定义表情资源及资源校验记录。
- `stackchan-build-info.json`：源码 SHA、运行链接、板卡/语言/唤醒词、SDK 镜像以及 lock、Pillow requirements、生成配置和输出文件的 SHA-256。
- `stackchan-licenses/`：项目、原厂、ESP-SR 和 ESP-IDF 许可，字体及已下载组件的可用元数据/通知，固定来源的官方字体许可原文与[资源来源说明](stackchan-assets-provenance.md)。资源 manifest 和构建信息均记录这些随包文件的 SHA-256。

这些是用于检查和后续设备验收的构建产物。流水线不上传 `merged-binary.bin`、bootloader、分区表或 NVS，不提供通用可刷包，也不执行 flash。应用与自定义资源须一起核对；构建过程内部产生的合并二进制仍使用默认基础资源，不能误当作已经包含自定义表情的交付包。

工作流使用 `contents: read`，checkout 不持久化 Git 凭据。PR 通过普通 `pull_request` 运行，没有 `pull_request_target`，不读取仓库 secret，不上传到外部服务，不发布 Release 或写入仓库。工件只保存在对应 GitHub Actions 运行中。

CI 成功表示该源码通过主机检查并能在固定环境下构建。真实设备的校准、方向、限位、显示屏和分区兼容性仍需按[板卡说明](../main/boards/m5stack/stackchan-k151/README.md)验收。以上固定输入减少环境变化；尚未宣称应用二进制跨机器逐字节一致，也未宣称通过本地检查等同于 GitHub 托管流水线已成功运行。

安装工具测试纳入 `python -m unittest discover -s tests -v`。构建还生成 `stackchan-install-package.zip`，包含独立 bootloader、分区表、OTA 初始化、应用、自定义资源、安装脚本、中文指南和来源/许可记录。它是 CI 开发工件，不等于已经实机验收的 GitHub Release；仍须完成本机校准、分区兼容和实机验收。生成器不会打入设备备份或 NVS。
