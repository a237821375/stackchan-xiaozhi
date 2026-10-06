# 表情资源来源与分发说明

[返回开发指南](stackchan-development.md)

表情由本仓库 `tools/stackchan-face-sources/factory-style-talking-20260929/generate.py` 用几何图形重新绘制；原厂眼睛/嘴巴比例参考 M5Stack StackChan，来源和 MIT 文本保存在 [factory_upstream](../main/boards/m5stack/stackchan-k151/factory_upstream/README.md)。不是从设备固件截取的图片。用户提供的实机照片仅用于 README 展示。

生成器使用固定 Pillow 版本，输出 21 种情绪、41 个 GIF 状态（困困无 talking 副本），不依赖 Mac 系统字体。字体与唤醒模型从**当前标准构建**的基础包原样保留，manifest 记录基础包和输出哈希及保留文件名，不把设备备份作为输入。

外部组件并不都受仓库根 MIT 统一覆盖：

| 资源/组件 | 来源与许可记录 |
| --- | --- |
| 文本字体/图标 | `78/xiaozhi-fonts` 2.0.0 组件元数据标注 Apache-2.0；`manifest.json` 列出 Noto Sans、SC/TC/JP/KR、Thai、Arabic、Emoji/Color Emoji 和 Material Symbols 的源字体路径。Noto 官方字体许可为 SIL OFL 1.1，Material 官方仓库采用 Apache-2.0；相应版权和许可原文保存在 [`docs/licenses`](licenses/README.md)。组件元数据的 Apache-2.0 声明不能替代底层字体许可。 |
| 唤醒词模型 | `espressif/esp-sr`，组件 `LICENSE` 为 ESPRESSIF MIT，授权条件限定 Espressif 产品并要求随副本保留版权与许可文本。ESP32-S3 属该产品范围；不能从本项目推断可移植到任意芯片。 |
| 原厂参考代码/几何 | M5Stack StackChan 固定来源与 MIT 文本见上述 factory_upstream。 |

本次只发布源码预发布，未发布整包固件或嵌入模型的 Release 资源下载。CI 开发工件自动附带 `stackchan-licenses/`：仓库根 MIT、原厂 StackChan/FTServo/smooth_ui_toolkit MIT、原厂来源记录、实际下载的 ESP-SR 许可、ESP-IDF 许可、字体组件 manifest/元数据/校验记录，以及已下载组件中能找到的 LICENSE/COPYING/NOTICE 和组件元数据。另附此说明及固定提交的官方字体许可原文，下载工件时须保留整个目录。

CI 会验证保存的官方许可文本 SHA-256，并在资源 `manifest.json` 的 `attribution_files` 及 `stackchan-build-info.json` 中记录随包的所有说明、组件元数据和许可文本哈希。许可证来源 URL 和提交见 [`upstream-license-sources.json`](licenses/upstream-license-sources.json)；这些提交固定的是许可原文，不是字体组件所使用的源字体版本。

当前字体组件包不含 `source/` 或独立 LICENSE/NOTICE；其公开元数据的旧仓库地址 `https://github.com/78/noto-fonts` 返回 404。已找到作者公开的 [78/xiaozhi-fonts](https://github.com/78/xiaozhi-fonts/tree/d45dbc64052d57048f20ab1770074172ce9eb53b)，固定提交对应 2.0.0 发布；本地组件的 102 个代码/字体/资源文件与该提交的 Git blob 逐字节匹配（元数据由注册表重排并增加来源字段）。该提交同样不含源字体或独立通知，因此组件源码身份已确认，但仍无法从包中追溯精确的底层字体版本或验证原始版权通知是否完全一致。现有官方字族许可作为来源记录随包保存，不能据此宣称已核实每个转换字形的完整许可链；组件 manifest 列出的字族也不代表每个字族在每份固件中都有实际字形。进一步发行字体或跨设备二进制前，需要向组件作者取得对应源字体版本和原始通知。新增字体、音频或其他素材时，在此记录来源、作者、许可及加工方式。当前资源不包含猫咪呼噜录音。
