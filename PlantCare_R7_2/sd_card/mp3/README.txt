TF 卡语音文件 (方案 R7.2 第 2 章, 语音版本记为 ver voice V1)

本包不附音频, 沿用 R7_Main 用的那张卡。现在卡上的三段 (文件名前 4 位是编号, 后面可以带名字):
  0001_thirsty.mp3        缺水 / 担忧脸   "I'm thirsty! A little water, please?"
  0002_enough_water.mp3   不缺水 / 笑脸   "I've had enough water. Thank you!"
  0003_soaking_in.mp3     等待 / 期待脸   "I'm still soaking it up! Let's wait a little."
(10-31 试听记下的实际录音。固件里打印的台词、无 TF 卡版本在屏幕上显示的台词都已改成这三句。)

注意: 方案第 2 章原定的三句是 "My soil seems dry. Could you check it?" /
"My soil reading is in range. No water is needed right now." / "Thank you. I am waiting for the water to soak in."
录音和方案不一致, 要么改方案, 要么按方案重录 (见项目文档 R7.2_待改清单.md)。

固件按 include/config.h 里的 CLIP_DRY = 1、CLIP_OK = 2、CLIP_WAIT = 3 播放, 和这张卡一致。
注意: 方案第 2 章的表写的是 0002 = 等待、0003 = 不缺水, 和卡上相反。二选一: 改方案的表,
或者把卡上 0002、0003 两个文件对调改名, 同时把 config.h 里 CLIP_OK、CLIP_WAIT 两行对调。
正式录音替换时编号保持不变, 并用 ver voice V2 记录新版本。

TF 卡要求:
  - 格式 FAT32, 卡根目录建 mp3 文件夹, 只放这三个文件
      Windows: 此电脑 -> 右键 TF 卡 -> 格式化 -> 文件系统选 FAT32 (32 GB 及以下的卡)
      Mac:     磁盘工具 -> 抹掉 -> 格式选 MS-DOS (FAT)
  - 文件名以 0001 / 0002 / 0003 开头 (例如 0001_thirsty.mp3); WAV 也可, 有的兼容芯片不认 WAV, 那就转成 MP3
  - Mac 拷完要在终端执行  dot_clean /Volumes/卡名  删除隐藏文件, 否则可能播错曲目 (Windows 不需要)
  - 装好后用 t7_dfplayer 检查: files on card: 3, 输入 1 2 3 都能听到
