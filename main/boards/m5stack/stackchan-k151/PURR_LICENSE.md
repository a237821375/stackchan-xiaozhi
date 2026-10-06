# 猫咪呼噜录音：来源与处理说明

状态（2026-10-06）：用户确认呼噜声尚未在实机实现。本文记录保留素材的来源、处理方式和代码预设，不是功能验收记录。

- 作者：Kerzoven
- 原作品：Cat Purr & Meow，文件 `cat_purrsleepy_loop.wav`
- 来源：[OpenGameArt 作品页](https://opengameart.org/content/cat-purr-meow)
- 原录音：[WAV 文件](https://opengameart.org/sites/default/files/cat_purrsleepy_loop.wav)
- 许可：[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)
- 获取日期：2026-09-29

`purr_pcm.h` 是处理后的衍生数据：24 kHz、单声道、有符号 16 位 PCM，循环接缝使用 40 ms 交叉淡化。可在 Python 3.11 环境中重新生成：

```sh
python tools/convert_purr.py /path/to/cat_purrsleepy_loop.wav
```

原始录音校验和记录在生成的头文件中。代码预设增益为原录音的 25%，淡入和淡出约 333 ms，不改变设备主音量。

代码中的预设策略为仅待机条件下允许播放，讲话、聆听、网络切换、提示音及待处理的解码任务优先。摸头状态已包含松手后约 3 秒的保留时间，音频不会再延长该时间。
