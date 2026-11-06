# PlantCare R7.2 固件包（R7.2-fw1.4）

对应《AI盆栽照护助手 项目方案 R7.2》。这是一个 PlatformIO 工程：10 个分模块测试程序（t1–t10）＋ 整机联通主程序（`app` 有 TF 卡，`app_noTF` 无 TF 卡）。

**接线与《R7 全流程操作指南 V6》完全相同，不用改线**（引脚表见 `include/pins.h`）。ESP32 经手机热点直接调用 Gemini，用的是板载 Wi-Fi，也不需要新连线。

**版本记录**

- **2026-11-06（只改 `t3_probe`）**：三级标定的同级极差判据由 5% 改为 10%（见第 6 节）。11 月 2 日首次标定最大 8.6%，团队决定放宽；按旧判据 `report` 会判 FAIL、不打印配置命令，所以当时的 `cal set` 是按这次数据手动输入的。`app`、`app_noTF` 和版本号 R7.2-fw1.4 都不变，不用重新烧录。
- **fw1.4（2026-11-04，按方案第二轮审核）**：改了三处，走近播报规则与 fw1.3 相同。
  - 解释文字不再说错大小关系。等待后未确认恢复的模板改为 `R is {R} now, but the last 3 readings were not all >= {H}. Please check again.`：最后一轮可能已经达到 H，只是最近连续 3 轮没有全部达到（例如 47、50、51）。缺水时 R 已达 H、不缺水时 R 刚低于 L（都还没连续 3 轮）这两种情况也换了说法。R 离 L 或 H 不到 0.5 时显示一位小数并向下截断，不会出现 `R 44 is below 44`。
  - 提示词改为 **P3**：等待后恢复与未恢复按"最近连续 3 轮"解释，不再写"等待后 R 仍低于 H"；`dry_to_ok`（没按键读数自己恢复）本身不说明有人补水；数字照抄输入。送给模型的输入多一项 `consecutive_at_or_above_H`（最近连续达到 H 的有效读数轮数）。`aidump` 里的提示词版本为 P3，烧录后不用改 `ver profile`（档案仍是 P2）。
  - 读数时好时坏的保护：植物状态下连续 15 轮都凑不齐"连续 3 轮有效"（例如有效、有效、无效反复出现，既不会连续 3 轮无效，也凑不齐 3 轮有效），就改为警示三角、停止植物语音，串口记 `state …->PAUSED unstable`；之后要连续 3 轮有效才恢复，原来在等待中的重新等待完整周期。原有的"连续 3 轮无效暂停、1 轮有效恢复"不变。参数 `UNCONFIRMED_ROUNDS_TO_PAUSE`（`config.h`，0 = 关闭），按 24 h 记录里无效读数的实际分布调整。
- **fw1.3（2026-11-04）**：只改日常模式（`talk normal`）下有人走近时的播报规则。状态机、阈值、录音和展示模式（`talk test`，每次走近都说）都不变。
  - 缺水：照旧，同一句至少隔 10 min。每天主动播报最多 6 次，现在只有缺水计入，不会再被不缺水那句占掉。
  - 等待水渗透：按键时说一次。等待中有人走近，距上一次等待语音过了半个等待时长（默认 5 min；`fast on` 时 30 s）就再说一次。
  - 不缺水：每次补水后恢复只说一次。恢复时 60 s 内有人走近就当场说，否则下一次有人走近时补说；之后走近不再说，直到状态改变。没按键但读数自己恢复（`dry_to_ok`）也一样；开机首次确定的不缺水不说。
  - 等待后未确认恢复（`wait_to_dry`）：再静默一个等待时长，才按缺水规则说"口渴"，免得刚浇完就请人再浇。静默期间读数回升会直接变绿。
  - 走近但不说话时，串口打印 `[voice] approach in <状态>: silent (<原因>)`，不写日志；`status` 多一行显示当前的走近播报规则和今天已主动播报的次数。
  - 参数在 `config.h`：`VOICE_WAIT_AGAIN_DIV`（等待中再说一次的间隔 = 等待时长 / 这个数）、`VOICE_RECHECK_QUIET_WAITS`（未确认恢复后静默几个等待时长）。规则本身在 `lib/PC_Care/VoicePolicy.h`，有单元测试。
- **fw1.2（2026-11-01）**
  - 屏幕只画图形，不写字。植物状态显示一个**动态表情**，风格沿用组员做的 HAPPY 表情：每 650 ms 上下轻晃，每 4.5 s 眨一次眼。
    - 不缺水：组员的开心脸（^ ^ 弯眼、腮红、U 形嘴），去掉了底下的 HAPPY 文字。
    - 缺水（含补水后未恢复）：担忧脸，八字眉、下弯嘴，右上角一滴汗慢慢滑下。
    - 等待：期待脸，带高光的亮眼睛、小圆嘴随呼吸变化，右上角三个点依次出现。
  - 判断不了植物状态时显示中性图标：开机和读数确认中是沙漏，未标定和探头异常是警示三角。屏幕画面只在动画帧变化时才刷新。
  - AI 解释不再上屏，只打印在串口（`[ai #N] AI: 标题 | 正文`），并写进 `aidump`。FAST / SIM 标记、R 值、等待倒计时、`NO VOICE` 也只在串口和日志里（`status` 有显示）。无 TF 卡版本的台词只打印在串口。
  - 展示植物改为花烛 *Anthurium crystallinum*：送给 AI 的档案按 RHS 植物档案（Moisture: Moist but well-drained），提示词改为 P2，不再写 Australian native plant。烧录后在串口输入 `ver profile P2`。
- **fw1.1（2026-11-01，台架 15 条通过后）**
  - 补水历史只记真实操作：`sim` 时按"已补水"不再写进闪存。改标定端点（`cal set` / `cal dry` / `cal wet`）或输入 `clear` 时，清掉补水历史和每小时 R，让 AI 解释的上下文只用本次标定下的真实记录。
  - `[round]` 行的 below / above 先计入本轮再打印，状态变化那一轮显示 3。
  - 开机记录 `boot fw=… reset=…` 改在挂载日志区后立刻写。上电几秒内断电或掉电重启，也能留下记录。
- **fw1.0（2026-10-31）**：首个联调版本，包括开机时自动擦除别的固件留下的日志文件系统。

---

## 0 先做这三件事

> **从 fw1.2 或 fw1.3 升级到 fw1.4**：直接编译并烧录 `app`（或 `app_noTF`）即可。设置区 `pc72`（标定、阈值、Wi-Fi、API 密钥、版本记录）和日志都保留，不用重新输入。烧录后 `status` 第一行应显示 `R7.2-fw1.4`，并多一行 `approach voice (talk normal): …`。烧录会重启一次，所以要在开始 24 h 连续运行（`clear`）之前烧录。下面三件事是从 R7 第一次换到 R7.2 时做的。

1. **先在你们电脑上全部编译一遍**（这一步**不用接开发板**）：Windows 双击 `flash_windows.bat` 选 `c`，Mac 双击 `烧录.command` 选 `c`。第一次编译要联网下载 ESP32 平台和工具链（几百 MB），需要几分钟到十几分钟。
   本包在我这边做过三项检查：
   - 用替身头文件对全部代码做 C++ 语法与类型检查，C++11 和 C++17 都通过；
   - 在电脑上跑 376 项单元测试（状态机、AI 校验、日志统计等），全部通过；
   - 在电脑上用模拟的时钟、传感器、Wi-Fi 和 Gemini 返回，真正运行整机主程序 `app` 和 `app_noTF`。第 8 节台架流程、AI 解释、日志、安静时段、探头断开等 60 多项检查全部通过，另外模拟了一次 24 h 连续运行。

   另外，一次独立审查把替身头文件换成 Arduino-ESP32 2.0.17 和三个库的真实头文件，再做一次语法检查：0 错误、0 警告。审查提出的问题已全部修改。

   **但没有用 ESP32 编译器编译和链接过，也没有上板跑过**，因为我这边的环境不允许下载 ESP32 工具链。第一次编译如果报错，把报错整段发回来。
2. **旧日志先导出**：本包把分区表换成了 `huge_app`（程序区 3 MB，日志区 896 KB）。第一次烧录后，板子上 R7 程序的旧日志就读不到了。旧日志如果还要用，先用旧程序 `dump` 存下来。
3. **设置要重新输入一次**：新程序用新的设置区 `pc72`，标定、阈值、Wi-Fi、API 密钥都要重新输入（见第 4 节）。旧标定的毫伏值可以直接用 `cal set` 写回。

---

## 1 包里有什么

```
PlantCare_R7_2/
├─ platformio.ini        12 个环境: t1…t10 + app + app_noTF; 串口记录自动存到 logs/
├─ flash_windows.bat     Windows 双击: 选编号 → 编译、烧录、开串口 (c = 全部编译, 不用接板子)
├─ 烧录.command          Mac 版的同一个菜单
├─ include/
│  ├─ pins.h             引脚表 (与指南 V6 一致); OLED 花屏改 OLED_VARIANT
│  ├─ config.h           方案参数 (采样、阈值规则、语音规则、AI 限额、日志) —— 改参数只改这里
│  ├─ root_ca.h          Google 根证书 GTS Root R1 + R4 (HTTPS 校验, 有效到 2036)
│  └─ sysutil.h          复位原因 (判断 BROWNOUT)
├─ lib/                  模块 (每个文件夹一个模块, 测试程序和主程序共用)
│  ├─ PC_Probe           SEN0308 读数: 每轮 7 次取中位数, 判断有效/无效
│  ├─ PC_Care            三状态状态机 (方案第 6 章): 连续 3 轮、滞回、等待复测、暂停、读数不稳暂停 (fw1.4)、重启规则; VoicePolicy.h 走近播报规则 (fw1.3)
│  ├─ PC_Display         OLED: 三种动态表情、两个设备图标 (沙漏、警示三角), 不写字
│  ├─ PC_Lights          灯环 12 颗 + 板载 RGB
│  ├─ PC_Voice           DFPlayer 三句语音 (NO_TF 时台词打印在串口)
│  ├─ PC_Presence        PIR: 60 s 预热、走近边沿、卡死检测
│  ├─ PC_Button          "已补水"按钮防抖
│  ├─ PC_Store           设置保存在 NVS (掉电不丢)
│  ├─ PC_Logger          LittleFS 日志: log.csv / ai.jsonl, dump、logstat
│  ├─ PC_Net             Wi-Fi 两组轮流连接 (家里 / 热点)、NTP 校时、悉尼时区、安静时段
│  ├─ PC_Ai              Gemini 调用 (后台任务, 不卡主循环)、重试一次、回退模板
│  ├─ PC_Explain         解释的输入 JSON、提示词、请求体、返回解析、本地校验、模板
│  ├─ PC_Json            小型 JSON 解析
│  ├─ PC_Util            标定计算 (CalMath)、命令分词 (Tokens)、日志统计 (LogStats)
│  └─ PC_Console         串口命令行
├─ src/
│  ├─ tests/t1…t10       分模块测试程序 (每个 env 只编译一个)
│  └─ app/main.cpp       整机联通主程序
├─ test_host/            电脑上的检查: 单元测试、替身头文件编译检查、整机主程序模拟联调 (不需要开发板)
├─ tools/
│  ├─ analyze_logs.py    把串口导出的日志整理成报告数据 (24 h 指标、状态时间线、AI 成功率与延迟、核对表)
│  └─ gemini_post.py     用和 ESP32 一样的请求体在电脑上调 Gemini (调提示词用)
└─ sd_card/mp3/README.txt  TF 卡音频要求 (音频沿用 R7_Main 卡上的三段; 编号对应关系在 config.h 的 CLIP_DRY / CLIP_OK / CLIP_WAIT)
```

---

## 2 编译、烧录、串口

**什么时候接开发板**：编译不需要开发板；烧录（`-t upload`）和看串口时才用 USB 线接上 FireBeetle。模块的线保持现在的接法，USB 线同时给板子供电。

### Windows

1. 安装 VS Code，在扩展里搜索安装 **PlatformIO IDE**。
2. 把包解压到**路径里没有中文和空格**的文件夹，例如 `C:\PlantCare\PlantCare_R7_2`。不要放在桌面、下载文件夹或 OneDrive 同步的文件夹里，路径太长或正在同步时容易编译出错。
3. VS Code → 文件 → 打开文件夹，选 `PlantCare_R7_2`。第一次打开时，PlatformIO 会自动下载 ESP32 平台、工具链和三个库，等右下角的进度结束。
4. 双击 `flash_windows.bat`，输入 `c` 回车，全部编译一遍，最后应显示 `All 12 programs built OK.`。
5. 用能传数据的 USB-C 线接上开发板。在设备管理器 → "端口 (COM 和 LPT)" 里，应出现 `USB-SERIAL CH340 (COMx)`。没出现时：先换一根线（有的线只能充电）；还不行就装 WCH 的 CH340 驱动（CH341SER.EXE），再重新插线。
6. 烧录：`flash_windows.bat` 输入编号（例如 `1` = t1_i2c_scan）回车，程序会编译、烧录，再打开串口。**串口就开在这个黑色窗口里**，命令直接在这里输入。按 `Ctrl+C` 退出；窗口问 `Terminate batch job (Y/N)?` 时按 `N`，回到菜单。
   一个 COM 口同一时间只能被一个程序打开：这个窗口的串口开着时，VS Code 的 🔌 会打不开；反过来 VS Code 的串口开着时，这里烧录会失败。
   菜单里的 `e` 会擦除板子上的整块闪存（要输入 `YES` 确认），只在 `app` 开机就反复重启、自动修复也没用时才用，见第 12 节；擦除后设置都没了，要重新输入 `wifi` / `wifi2` / `apikey`。
7. 也可以只用 VS Code：底部状态栏选环境（例如 `env:t3_probe`），点 → 上传，再点 🔌 打开串口。在串口窗口里可以直接打字发命令。
8. 电脑上有多个 COM 口、PlatformIO 选错时，在 `platformio.ini` 的 `[env]` 下加两行，例如 `upload_port = COM5` 和 `monitor_port = COM5`。

### Mac

- 双击 `烧录.command`，输入编号回车。第一次双击如果提示"无法打开"，在 Finder 里右键 → 打开。
- 终端：`~/.platformio/penv/bin/pio run -e t3_probe -t upload -t monitor`。
- 需要先装 WCH 的 CH34x 驱动（指南 7.1 节）。

### 两边相同

- 烧录卡在 `Connecting...` 时：按住板上 BOOT，再点一下 RST。
- 第一次编译会自动下载 U8g2、Adafruit NeoPixel、DFRobotDFPlayerMini 三个库，需要联网。下载后装在 `.pio/libdeps/`，之后不会自动升级。组员各自的电脑第一次编译时，可能拿到不同的小版本，编译报错时先对一下 `pio pkg list` 的版本号。
- **串口记录自动保存**：每次打开串口，`logs/` 文件夹里都会多一个 `device-monitor-日期-时间.log`，每行带时间。这些就是报告素材，`tools/analyze_logs.py` 直接读它们。
- 主程序 app 会把输入的命令回显成 `> 命令`，这样记录里能看出做了什么。`apikey` 和 Wi-Fi 密码回显成 `****`，不会进记录。

---

## 3 分模块测试 t1–t10

按顺序做。t1–t9 与指南 7.3 节相同（t3、t8、t9 有增加），t10 是新的。

| 环境 | 测什么 | 怎么操作 | 合格标准 |
|---|---|---|---|
| `t1_i2c_scan` | OLED 是否连上 | 烧录后看串口 | `found 0x3C` |
| `t2_oled` | 屏幕显示 | 串口输入 `n` 下一页，`a` 自动轮播 | 三张动态表情（开心、担忧、期待：轻晃、眨眼、汗滴、三个点）和两个图标（沙漏、警示三角）都清楚，不花屏、不偏移（花屏：`pins.h` 里 `OLED_VARIANT` 改成 2）；每页 6 s |
| `t3_probe` | 探头读数 / 三级标定 | 器件检查：探头固定不动（空气里平放在桌上；水里夹在杯边、深度固定且不过警示线），等出现两条 `10 rounds` 统计行，再在同一位置连续输入 `a`（或 `w`）2–3 次，最后输入 `report`。重做前先输入 `reset`，清掉上一次的记录。三级标定见第 6 节 | 差值 > 600 mV，同一状态极差 < 20 mV，全部 < 2450 mV。水里读数停在约 142 mV、极差 0 是正常的（ESP32 能测的最低值，见第 6 节） |
| `t4_pir` | 走近触发 | 先等 60 s，再在 1–3 m 外走过 10 次 | ≥ 9/10 次触发；人不动时不触发 |
| `t5_button` | 按钮 / 碰线 | 按（碰）10 次 | 计数正好 10 |
| `t6_ring` | 灯环 + 板载灯 | 自动轮换；`y` `b` `g` 黄蓝绿，`o` 关，`w` 白，`c` 跑马灯，`+` `-` 亮度，`a` 自动 | 12 颗都亮；黄、蓝、绿正确；第一颗不闪 |
| `t7_dfplayer` | 三句语音 | 输入 `1` `2` `3`（几秒后打出 `finished N`），`+` `-` 音量，`s` 停止，`r` 重新初始化，`d` 不经过库直接查询，带应答和不带应答各一次（两次都是 `none` = 模块一个字节都没回，查供电和 TX/RX 线） | `files on card: 3`；三句都能播，1 m 外听得清，不破音（音量 ≤ 20） |
| `t8_power` | 供电压力 | 用展示时的真实供电跑 10 min，人别走动。灯环全亮、三句循环播放、Wi-Fi 每 20 s 做一次 HTTPS；已存 API 密钥时，每 60 s 在播放语音的同时真实调用一次 Gemini。`f` 切换满亮度（最坏情况），`s` 重新开始 | 不重启；复位原因不是 BROWNOUT；PIR 误触发 ≤ 1 次。中途重启的话，下次上电会打印 FAIL 和复位原因 |
| `t9_selftest` | 全部一起 | 看串口；按按钮切换 笑脸/担忧/期待（同时换灯色、播语音）；在 PIR 前走过（10 s 后有效），板载灯闪 | OLED、探头、DFPlayer 全部 PASS；Wi-Fi、校时显示 PASS 或 SKIP（未设置）；表情、灯、声音对应 |
| `t10_ai` | Wi-Fi + Gemini 解释 | 见第 7.3 节 | `runall` 每条都是 `AI`；`bad` 前 5 条 REJ、最后 1 条 PASS |

t8 和 t10 使用主程序保存的 Wi-Fi 和密钥。所以先在 `t10_ai`（或 `app`）里输入 `wifi …` 和 `apikey …`，再跑 t8。

---

## 4 整机主程序 app：第一次设置

```
pio run -e app -t upload -t monitor        # 屏幕显示警示三角 (未标定)
```

在串口里依次输入（`#` 后面是说明，不用输入）：

```
wifi MyHomeWiFi homepassword       # 第 1 组: 家里 Wi-Fi (实盆 24 h 运行时用)
wifi2 PlantHotspot hotspotpass     # 第 2 组: 手机热点 (展示现场用), 两组轮流尝试
apikey 你的Gemini密钥                # 只存在板子上, 不会打印出来
cal set 2101 1152                  # 干端、湿端毫伏, 来自 t3 的 report (第 6 节)
cal id C1                          # 标定版本
set LH 47 52                       # 临时阈值 v0: L = t3 report 给出的中间级 R, H = L + 5
ver param v0-temp                  # 阈值版本
set wait 10                        # 等待水渗透的分钟数, 按实测补水时间改
id pot POT1                        # 盆号
id probe SEN0308-1                 # 探头编号
status                             # 检查全部配置
```

**热点要求**（方案第 1 章待确认事项）：

- ESP32 只能连 **2.4 GHz**。iPhone：设置 › 个人热点，打开"**最大兼容性**"。Android：热点频段选 2.4 GHz。
- 热点名称**不要有空格和撇号**。iPhone 热点名就是手机名，可在 设置 › 通用 › 关于本机 › 名称 里改，例如 `PlantHotspot`。
- 名称里实在有空格时，用英文双引号：`wifi2 "Plant Hotspot" pass123`。
- 连上以后，`status` 里会显示 `wifi connected` 和当前时间。校时后安静时段（21:00–07:30）才生效。时间未知时程序保守静音，台架测试可先 `quiet off`。

`status` 一屏显示：状态与 R、标定与阈值、版本号、语音（模块、文件数、talk 模式、是否静音）、PIR、按钮、OLED 与当前画面（`screen face:worried` 之类）、Wi-Fi 与时间、AI（开关、密钥、模型、今日调用次数、上一次结果）、日志占用、剩余内存。

---

## 5 命令一览（app）

| 命令 | 作用 |
|---|---|
| `status` / `help` / `time` / `reboot` | 状态总览 / 命令列表 / 当前时间 / 重启 |
| `cal dry` / `cal wet` | 现场测 5 轮，记为干端 / 湿端（读数要在 200–3000 mV 之间；正式标定用 `cal set`） |
| `cal set 2101 1152` / `cal id C1` | 写入干端、湿端毫伏 / 标定版本号 |
| `set LH 47 52` | 同时设缺水下限 L 和恢复阈值 H（H 必须大于 L）。从台架值换成实盆值时用它，不受先后顺序影响 |
| `set L 47` / `set H 52` / `set wait 10` | 只改 L / 只改 H / 等待分钟数 |
| `ver profile P1` / `ver voice V1` / `ver param v0-temp` | 档案、语音、阈值版本 |
| `id pot POT1` / `id probe SEN0308-1` | 盆号、探头编号 |
| `sim 25` / `sim off` | 注入模拟 R（`status` 和日志标 SIM，屏幕不显示；模拟时的按键不写进补水历史）/ 回到真实读数 |
| `fast on` / `fast off` | 台架快速参数：5 s 采样、等待 1 min（标 FAST；重启后自动关闭） |
| `quiet on` / `quiet off` | 安静时段 21:00–07:30 是否生效 |
| `talk test` / `talk normal` | 走近就说（两次至少隔 8 s）/ 日常规则（fw1.3：缺水隔 10 min、每天最多 6 次；等待过半再说一次；不缺水每次恢复只说一次；未确认恢复先静默一个等待时长）|
| `volume 20` | 音量 0–30 |
| `press` / `motion` | 模拟按键 / 模拟有人走近 |
| `explain` | 立刻按当前状态请求一次 AI 解释（两次至少隔 10 s） |
| `ai on` / `ai off` | 开关 AI 解释。`ai off` 时不联网调用，串口和日志里也没有解释 |
| `apikey <KEY>` / `model <id>` | 保存密钥 / 换模型（`model` 不带参数 = 默认 `gemini-3.5-flash-lite`） |
| `wifi <名称> <密码>` / `wifi2 …` / `wifi clear` | 第 1、2 组网络 / 清空两组。保存后立即重连，不用重启 |
| `dump` / `aidump` | 导出采样+事件日志 / AI 解释日志 |
| `logstat` | 24 h 测试统计（第 9 节） |
| `clear` | 清空日志和补水历史（开始 24 h 测试前用） |
| `factory yes` | 清空全部设置和日志并重启 |

改了干湿端而 L/H 已经设置过时，程序会打印换算后的 `set LH …`，让判断边界的毫伏值保持不变（方案第 5 章：端点冻结后再定 L/H）。

---

## 6 三级标定（方案第 5 章）

用 `t3_probe`。字母区分大小写：大写 `D` `M` `W` 用于标定；小写 `a` `w` 用于到货检查。

1. **到货检查**：探头平放在桌上（手不要碰感应部分），等两条 `10 rounds` 统计行后，不动探头连续输入 `a` 2–3 次；再把探头固定在一杯清水里（深度固定、不过警示线），等读数稳定后连续输入 `w` 2–3 次，最后输入 `report` → `device check … PASS`。这一步看的是探头不动时读数稳不稳，两次之间不要重新插拔；插拔造成的差别由第 5 步的三级标定判定。
   如果任何读数 ≥ 2450 mV，**标定前**先加 10 kΩ/20 kΩ 分压。之后不再改动探头到 ADC 的电路。
2. **干端 D**：烘干基质装入容器，按标记深度和方向插入。等串口提示 `settling 3 s` 结束后自动记录一次。拔出、擦干、按同一深度重插，共 3 次，每次输入 `D`。
3. **中间级 M**：约一半湿端含水量的样品（揉匀、封袋静置一夜），同样插 3 次，每次输入 `M`。
4. **湿端 W**：浇透、盖住沥干 30–60 min 后，同样插 3 次，每次输入 `W`。
   读数应高于 200 mV。ESP32 在 11 dB 衰减下能测的最低值约 142 mV：探头放在清水里、拔掉探头、探头没供电，读数都会停在这个值不动（spread 0）。所以主程序把低于 200 mV 的读数一律当作无效（连续 3 轮后暂停判断，屏幕显示警示三角），不会当成"很湿、不缺水"。如果 W 读数也停在约 142 mV，说明样品湿到超出了量程，多沥一会儿再测，`report` 会给出提醒。
5. 输入 `report`，得到：
   ```
   calibration: span 949 mV, pooled sd 2.8, span/sd 339.0 (>=10 ok)
                monotonic ok, max range 0.6% of span (<10% ok) -> PASS
     write to the app:
       cal set 2101 1152
       cal id C1
     provisional thresholds v0 (mid level R = 47.4):
       set LH 47 52
       ver param v0-temp
   ```
   判定规则：跨度 ≥ 10 × 三级合并标准差、三级单调、同一级极差 < 跨度 10%（原定 5%；11 月 2 日首次标定最大 8.6%，团队决定放宽到 10%，值在 `lib/PC_Util/CalMath.h` 的 `CAL_MAX_RANGE_PCT`）。**FAIL 时不要启用三状态**，先检查插入深度、压实程度，或换探头。
6. 烧录 `app`，把打印出来的命令原样输入。
7. 探头插入实盆、浇透稳定后复核湿端：`t3` 的数据流或 `app` 的 `status` 里看毫伏。偏移明显时，在设 L/H **之前**重新 `cal set`，并换 `cal id C2`。之后探头不再拔出。

`reset` 清空记录重来，`s` 开关每秒的数据流。

---

## 7 AI 照护解释（方案第 2、6 章）

### 7.1 它做什么、不做什么

- **触发**：照护状态变化时（first_state、ok_to_dry、dry_to_wait、wait_to_ok、wait_to_dry、dry_to_ok），等 5 s（让语音先播完），ESP32 经 Wi-Fi 调用 Gemini。
- **输入**：ESP32 已经算好的数字，包括当前 R、L、H、本地时间、距上次补水小时数、补水前 R、复核时 R、近 12 h 下降速度、上一周期小时数、近 24 h 每小时 R，以及档案摘要（植物名、需水描述、资料来源）。模型不做任何判断。
- **输出**：一个标题（≤ 20 字符）和一两句说明（≤ 100 字符），英文 ASCII。
- **本地校验**：数字必须出现在输入里；不能出现诊断或感受类词（root、rot、heat、sun、feel、thirsty 等）；不能和当前状态矛盾（例如不缺水时叫人补水）。不合格时带着原因重试一次，仍不合格就用固定模板。
- **显示**：fw1.2 起屏幕只显示表情，解释不上屏。结果打印在串口：`[ai #N] AI: 标题 | 正文`，并记一条 `ai_result AI <事件> ok <延迟>`；断网、没密钥、超时、超出额度或校验不过时是 `TPL`（模板）和原因。每条都写进 `aidump`。展示时把电脑上的串口窗口一起给人看。
- **不做的事**：不改变任何状态判断、阈值、计时或语音；不转成语音；设备状态（开机、读数确认中、未标定、探头异常）下不调用。
- **限额**：两次自动调用至少隔 60 s；每天最多 50 次请求（重试也算一次），重启后从 0 重新计。连接、握手、等待返回各最长 8 s，网络很差时一次最长可能半分钟，但都在后台进行，不影响采样、灯、按键和语音。剩余内存或最大连续内存块不足时，直接用模板。
- **安全**：HTTPS 校验 Google 根证书；密钥只存在板子上，串口和日志都不显示。

### 7.2 默认模型

`gemini-3.5-flash-lite`（2026-10 核对过：Gemini API 模型列表里是稳定版，默认思考等级 minimal，延迟低）。Google 换型号时用 `model <新 id>` 修改，不用重新烧录。

### 7.3 用 t10_ai 先在台架上测

```
wifi2 PlantHotspot hotspotpass
apikey 你的密钥
status            # wifi connected, time 14:20, key set, model gemini-3.5-flash-lite
run ok_to_dry     # 打印输入 JSON、原始返回、校验结果、最终文字 (只在串口)
runall            # 8 种情境依次跑一遍 (每条间隔 3 s)
bad               # 不联网: 6 条示例输出演示本地校验, 前 5 条违规应为 REJ 并写明原因, 最后 1 条合规应为 PASS
tpl               # 打印全部模板
body ok_to_dry    # 只打印请求体, 配合 tools/gemini_post.py 在电脑上调提示词
```

合格标准：`runall` 每条都是 `AI`。每条都会打印延迟，方案第 7 章要统计 p50/p95（第 9 节的工具会算）。出现 `TPL` 时看 `reason`：

| reason | 含义 | 处理 |
|---|---|---|
| `offline` | 没连上 Wi-Fi | 检查热点 2.4 GHz、名称密码 |
| `no_api_key` | 没存密钥 | `apikey …` |
| `http_400 …` / `http_403 …` | 请求或密钥被拒 | 看后面的错误信息；检查密钥、模型 id |
| `http_404 …` | 模型 id 不存在 | `model gemini-3.5-flash-lite` |
| `http_429 …` | 超出免费额度 | 等一会儿；减少 `runall` |
| `http_error: …` | 连接或 TLS 失败 | 热点有没有网；试 `insecure on`，能通说明是证书问题，记下来反馈（测完 `insecure off`） |
| `invalid: …` | 两次输出都没通过本地校验 | 正常的安全回退；把这条记下来作为提示词改进素材 |

### 7.4 在主程序里

- 状态变化后约 5 s 调用，串口打印结果；屏幕一直只显示表情。
- 串口 `explain` 可以随时按当前状态再要一次（演示用）。
- `aidump` 导出每次解释的记录：时间、事件、模型和实际版本、提示词版本（fw1.2 起 P2，fw1.4 起 P3）、格式版本 S1、HTTP 码、尝试次数、延迟、AI/TPL、原因、显示文字，以及**送给模型的输入**。方案第 7 章的两人核对就基于这份记录（见第 9 节的 `ai_review.csv`）。
- 不想调用时 `ai off`，行为与 R7 完全相同。

---

## 8 台架快速跑一遍（指南 8.1 的 13 条，R7.2 版）

```
cal set 2400 400   # 电位器台架用的临时标定 (2400 mV 当干端, 400 mV 当湿端); 电位器不要拧到两头, 两头超出有效量程
cal id BENCH
fast on        # 5 s 采样、等待 1 min (status 和日志标 FAST; 屏幕不显示)
quiet off      # 还没校时也能出声
set LH 30 38   # 台架用阈值 (电位器); 实盆换回第 6 节的值, 例如 set LH 47 52
talk normal    # 测日常播报规则 (fw1.3); 现场演示用 talk test
```

台架做完后，用第 6 节 `report` 给出的命令换回实盆标定和阈值（`cal set …`、`cal id C1`、`set LH …`）。表中 1–13 与指南 8.1 相同。R7.2 的变化：**状态变化后约 5 s，串口会打印 AI 解释（AI 或 TPL）**，屏幕始终只显示表情或图标；第 12 条的重启规则写得更细。倒计时用 `status` 看（`waiting: N s left`）。

| 做什么 | 应该看到 | 验证的规则 |
|---|---|---|
| 1. 电位器拧到中间偏湿（R 约 50） | 约 15 s 后：笑脸、绿灯 | 连续 3 轮 ≥ H 才判定不缺水 |
| 2. 在 PIR 前走过（上电 60 s 后） | 不出声，串口 `[voice] approach in OK: silent (ok_already_said)`（开机首次确定的不缺水不说）。输入 `talk test` 再走过，播放不缺水那一句（现在的卡上是 0002）；测完改回 `talk normal` | 日常模式只在状态变化后说；展示模式每次走近都说 |
| 3. 碰一下按钮 | 串口 `button_ignored_in_OK`，什么都不变 | 不缺水时按键被忽略 |
| 4. 电位器拧向干（R < 30） | 约 15 s 后：担忧脸、黄灯 | 连续 3 轮 < L 才判定缺水 |
| 5. 再走过 PIR | 播放 0001 | 说当前状态那一句 |
| 6. 马上再走过 | 不出声，串口 `speak_skipped_cooldown 1` | 同一句有冷却 |
| 7. 碰一下按钮 | 立刻：期待脸、蓝灯，播放等待那一句（现在的卡上是 0003）；`status` 显示 `waiting: 60 s left` | 只有缺水时才接受按键 |
| 8. 等待期间把电位器拧回湿（R > 38）；按键后 30 s 内走过一次，过了 30 s 再走过一次 | 不会立刻变绿，倒计时继续。第一次走过不出声（`silent (waiting_said_recently)`），第二次再说一次等待那一句（`speak 3 (waiting_again)`） | 等待期没到不判断恢复；等待过半才再说一次 |
| 9. 倒计时结束，之后再走过一次 | 笑脸、绿灯；60 s 内有人走近过则当场播不缺水那一句，否则下一次走近时补说。之后再走过不出声（`silent (ok_already_said)`） | 到期且最近连续 3 轮 ≥ H → 不缺水；恢复那句每次恢复只说一次 |
| 10. 重复 4、7，但一直保持干；到期后 1 min 内走过一次 | 到期后回到担忧脸、黄灯，串口 `state WAITING->DRY wait_to_dry`；走过不出声（`silent (recheck_quiet)`） | 没恢复就回到缺水；未确认恢复先不请人再浇水 |
| 11. 电位器停在 R 约 34 | 保持原状态，不来回跳 | 滞回区 |
| 12. 停在 R 约 34，重启 | 分两种情况。**时间未知**：关掉热点（或家里 Wi-Fi 不可用），**拔掉 USB 再插上**，屏幕显示沙漏，灯熄，不默认变绿。注意 `reboot` 是软件重启，ESP32 会保留时间，看不到这一支。**时间已知**：输入 `reboot`，或拔电后能连上 Wi-Fi；若 6 h 内有原状态记录（每 5 min 更新一次），约 2 min 后沿用原状态。两种情况下，重启后 fast 都自动关闭（60 s 一轮）；原来在等待中则重新等待完整周期 | 开机规则 |
| 13. 输入 `dump` | 打印完整日志 CSV | 日志可导出 |
| 14.（新）连上热点后重复 4 | 黄灯后约 5 s，串口 `ai_result AI ok_to_dry ok`，内容与 R 和阈值一致；`aidump` 有记录 | AI 解释与日志 |
| 15.（新）关掉手机热点后重复 7 | 串口 `ai_result TPL dry_to_wait offline`，状态、灯、语音照常 | 断网只停 AI |

台架 fast 模式下状态变得很快，而两次 AI 调用至少隔 60 s。所以有的变化会晚一点才有解释；排队中的请求遇到下一次状态变化会被替换，过时的解释不会发出。这是设计如此。

---

## 9 24 h 连续运行（方案第 7 章）

实盆按**正常参数**运行（不开 `fast`、不开 `sim`）。前 24 h 就是连续运行测试。

1. 开始前输入 `status`，逐项确认：已校时（连家里 Wi-Fi 最稳）；`quiet on`、`talk normal`（台架测试改过的这两项会一直保存，要改回来）；屏幕没有 FAST / SIM；阈值是实盆的 `set LH` 值。然后输入 `clear`（同时清掉台架留下的补水记录），并记下开始时间。
2. 运行 24 h 以上，期间照常人工检查、称重、记录。
3. 输入 `logstat`：
   ```
   sample rows: 291 | valid: 290 | FAST/SIM rows: 0
   time axis: NTP clock | span 24.17 h | expected 291 rows | coverage 100.0 % (valid 99.7 %)
   longest gap between sample rows: 5.0 min
   first 24 h: 288 rows, 287 valid (need >= 274 of 288) -> PASS
   boots logged: 0 (none) | abnormal resets (BROWNOUT/PANIC/WDT): 0
   state changes: 1
     11-01 22:06  OK->DRY ok_to_dry R=38.0
   ```
   期间有重启时，`boots logged` 会列出每次的复位原因。合格：`first 24 h … PASS`（288 条中有效 ≥ 274 条）、异常复位为 0、FAST/SIM 为 0，状态变化与人工记录对得上。
4. **马上导出**：输入 `dump`，等 `END LOG`；再输入 `aidump`。记录已经自动存在 `logs/` 里。
5. 在电脑上整理（Windows 用 PlatformIO 自带的 Python，不用另外安装）：
   ```
   Windows:  "%USERPROFILE%\.platformio\penv\Scripts\python.exe" tools\analyze_logs.py logs\*.log --out report_24h --start "2026-11-01 09:00"
   Mac:      python3 tools/analyze_logs.py logs/*.log --out report_24h --start "2026-11-01 09:00"
   ```
   得到 `summary.md`（全部指标）、`log.csv`（可用 Excel 打开，带本地时间列）、`ai.jsonl`，以及 `ai_review.csv`（每条 AI 解释一行，含送给模型的输入，留好两名成员的标注列：是否有输入之外的数字或断言、是否与当前状态矛盾）。

日志每 5 min 一条采样，事件随时记录。单个文件 128 KB，保留两个，大约够一周。

---

## 10 异常与断网（方案第 7 章，各测 3 次）

| 做什么 | 应该看到 |
|---|---|
| 拔掉探头插头 | 读数跳动，或落到 ESP32 能测的最低值（约 142 mV，低于 200 mV 即视为无效），连续 3 轮无效后：屏幕显示警示三角，灯环熄，不说植物台词、不调用 AI；插回后 3 轮有效读数再判断（原来在等待中则重新等待完整周期）。GPIO34 上接了 100 nF 电容时，电压要慢慢放掉，记录实际用了几轮 |
| 探头接触时好时坏（fw1.4；`fast on` 时每轮 5 s：插着 10 s、拔下 5 s，反复约 2 min） | 不会连续 3 轮无效，也凑不齐 3 轮有效。约 15 轮后屏幕显示警示三角、灯环熄、不说植物台词，串口 `state …->PAUSED unstable`；稳定插好后连续 3 轮有效才恢复（原来在等待中则重新等待完整周期） |
| 错标定（`cal set` 干湿差 < 300 mV；或写入别的探头 / 旧版本的标定） | 差值过小时拒绝写入。版本写错时程序无法自己发现，但 `status` 和每条 `boot` 日志都带 `cal id`，可以核对；R 超出 0–100 时串口标 `(out of 0-100)` |
| 拔掉 TF 卡（有卡版本） | 模块能回消息时：状态、屏幕、灯照常，串口 `speak_skipped_no_voice`；插回后自动恢复（模块报告插卡，或每 10 min 一次最多约 1.5 s 的轻量重试）。**你们现在的模块从不回消息**，固件是"只发不收"（见第 12 节），这项测不出来，只能靠听 |
| PIR 持续高电平 5 min（例如 OUT 临时接 3V3） | 日志 `pir_fault`，停止走近触发；屏幕和灯照常；恢复低电平后自动恢复 |
| 关掉热点 | 状态照常；串口 `ai_result TPL … offline`；热点恢复后自动重连 |
| `reboot` / 拔电再上电 | 见第 8 节第 12 条；日志 `boot … reset=…` 记录复位原因 |

---

## 11 展示前检查清单

- [ ] `status`：calibrated yes、版本号正确、voice online files 3、wifi connected、time 正确、ai on、key set
- [ ] 手机热点已开（2.4 GHz / 最大兼容性），名称无空格；备用热点已用 `wifi` 或 `wifi2` 存好
- [ ] `explain` 一次，串口显示 `ai_result AI manual ok`
- [ ] 音量 1 m 外听得清；现场演示用 `talk test`，长期运行用 `talk normal`
- [ ] `quiet on`（台架测试时关掉过的话；展示在 21:00 以后则临时 `quiet off`，展示完改回）
- [ ] 没开 `fast`、`sim`（屏幕不再显示这两个标记，用 `status` 确认：没有 `FAST` 和 `SIM on`）
- [ ] 用展示时的真实供电跑过 `t8_power`
- [ ] 24 h 数据和 AI 记录已导出

---

## 12 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 编译报错 `pins.h` / `config.h: No such file` | `platformio.ini` 的 `build_flags` 里少了 `-I include` | 用本包的 `platformio.ini` |
| Windows 编译报奇怪的路径错误、找不到文件 | 工程路径有中文、空格，或太长 | 把工程放到 `C:\PlantCare\PlantCare_R7_2` 再编译 |
| Windows 用户名是中文，工具链装不上或编译失败 | PlatformIO 默认装在 `C:\Users\用户名\.platformio` | 新建系统环境变量 `PLATFORMIO_CORE_DIR = C:\.platformio`，重启 VS Code 后重新编译 |
| Windows 设备管理器里没有 `CH340 (COMx)` | 充电线不能传数据，或缺驱动 | 换线；装 CH340 驱动 |
| VS Code 串口打不开 COMx（`could not open port` / `Access is denied`），或烧录报端口被占用 | 另一个程序正开着这个串口，常见是 `flash_windows.bat` 窗口里的串口 | 在那个窗口按 `Ctrl+C`（再按 `N`）或关掉它，再开另一个 |
| 开机第一行打印 `E (…) esp_core_dump_flash: Core dump data check failed` | 换了分区表，闪存里留着旧程序的数据 | 不影响使用，可以忽略 |
| `app` 开机就重启、循环，串口有 `assert failed: lfs_fs_grow_ … (block_count >= lfs->block_count)` | 闪存里留着 R7_Main 的日志文件系统（它的日志分区比本包大），挂载时库断言失败 | 新包开机挂载前会先检查：发现是别的固件留下的更大文件系统，就自动擦掉日志区再格式化，只做一次（串口打印 `[log] the log area holds a larger file system from another firmware (240 blocks, room for 224): erasing it once`，第一次开机多等十几秒）。紧接着的三行 `E (…) esp_littlefs: … Corrupted dir pair at {0x0, 0x1}`、`mount failed (-84)`、`Failed to initialize LittleFS` 是擦除后第一次挂载必然失败、库随即自动格式化的提示，只出现这一次；`status` 里 `log … bytes` 有数字就说明格式化成功。Wi-Fi 和密钥保留。万一还在循环，菜单选 `e` 擦除整块闪存，再选 `11` 烧录，重新输入 `wifi` / `wifi2` / `apikey` |
| 编译报错找不到板子或平台 | 本机 PlatformIO 的平台版本不同 | 把 `platform`、`board` 两行换成旧工程能编译的写法 |
| 上传失败 `port is busy` | 串口监视器还开着 | 先 `Ctrl+C` 退出串口 |
| OLED 花屏或偏移 | 驱动型号 | `pins.h` 里 `OLED_VARIANT` 改成 2 |
| `files on card: 0` | 卡没插好 / 不是 FAT32 / 文件夹名不是 mp3 | 见 `sd_card/mp3/README.txt` |
| `files on card: -1`，状态 `TX-only` | DFPlayer 能收命令、能放音，但从不回任何字节（R7_Main 10-29 的记录也是这样：tracks=-1、events=0） | 固件照样发播放命令，用 t7 按 1 2 3 试听确认。不能自动发现缺卡或模块故障。想恢复自动检测，要查 DFPlayer TX → GPIO16 这条线（青色线 j19 ↔ j28）或换模块 |
| PIR 一直没反应 | 本包已改成和 R7_Main 测试通过的 t4 一样用 `INPUT`（不加内部下拉，见 `config.h` 的 `PIR_PIN_MODE`） | 换新包重跑 t4；还是没反应再查硬件（第 3 节 t4） |
| 一直 Reading… | R 在 L 和 H 之间，且没有可信原状态 | 正常：等 R 低于 L 或达到 H；或连上 Wi-Fi 校时 |
| 屏幕一直是警示三角（`status` 里 state 是 PAUSED） | 读数无效（断线、接触不良、极差过大） | `status` 看 mV；检查黄线；跑 `t3_probe` |
| 不说话 | 时间未知或安静时段 / 冷却 / 每日上限 / 卡；日常模式下等待中还没过半、恢复那句已经说过、等待后未确认恢复的静默期（fw1.3） | 串口里的 `speak_skipped_…` 或 `[voice] … silent (…)` 写明了原因；`status` 看 quiet 和 approach voice 两行 |
| 解释总是 TPL | 见第 7.3 节的 reason 表 | `status` 里 ai 那一行的 `last` 写明原因 |
| Wi-Fi 一直连不上 | 5 GHz 热点、名称或密码错 | iPhone 打开"最大兼容性"；用引号包住有空格的名称 |

---

## 13 电脑上的检查（不需要开发板）

这些脚本需要 bash 和 g++（Mac、Linux；Windows 上要用 WSL），只是开发检查，烧录和使用都不需要它们。

```
bash test_host/run_tests.sh             # 376 项单元测试: 台架 13 条、重启与故障、JSON、AI 校验与模板、标定计算、分词、日志统计
bash test_host/run_sim.sh               # 主机联调: 在电脑上运行 app 和 app_noTF (模拟时钟、探头、PIR、按钮、Wi-Fi、Gemini 返回),
                                        #   按第 8 节台架流程、AI 解释 (合规 / 违规重试 / 429 / 断网 / ai off)、日志、安静时段、探头断开逐条检查;
                                        #   再查日志分区里留着旧固件文件系统时的开机处理 (更大的旧文件系统要擦掉, 本固件自己的要保留),
                                        #   以及补水历史 (sim 按键不写入, 改标定和 clear 时清掉)
bash test_host/compile_check.sh         # 用 test_host/stubs 的替身头文件, 对全部 lib、t1–t10、app、app_noTF 做语法与类型检查
bash test_host/compile_check.sh gnu++11 # 按 ESP32 Arduino 2.0 默认的 C++11 再查一遍
```

替身头文件只声明了本工程用到的 Arduino 和库函数，`test_host/sim` 给它们配了简单的模拟实现。它们用来尽早发现拼写、类型、格式字符串和流程逻辑上的错误，不能代替 ESP32 编译（`pio run`），也不能代替上板测试（真实的 ADC 读数、DFPlayer 应答、TLS 握手、供电）。

---

## 14 文件与方案章节对应

| 方案 R7.2 | 固件位置 |
|---|---|
| 第 2 章 三句固定台词、走近触发、talk test | `lib/PC_Voice`、`app` 的 `speak()` 与 `handleMotion()`、`lib/PC_Care/VoicePolicy.h`（fw1.3 走近播报规则）、`config.h` 语音段 |
| 第 2、6 章 AI 运行时解释、校验、回退、日志 | `lib/PC_Ai`、`lib/PC_Explain`、`app` 的 `requestExplain()` / `onAiResult()` |
| 第 3 章 引脚与供电 | `include/pins.h`、`t8_power` |
| 第 5 章 相对湿润指数 R 与三级标定 | `lib/PC_Util/CalMath.h`、`t3_probe`、`cal` 命令 |
| 第 6 章 三状态、连续 3 轮、滞回、等待复测、暂停、重启 | `lib/PC_Care/CareState.*`（单元测试 `test_host/test_pure.cpp`） |
| 第 6 章 降级（TF 卡、PIR、热点、API） | `app` 的 `loop()`、`speak()`；`lib/PC_Presence` |
| 第 7 章 24 h 连续运行、AI 成功率与延迟、两人核对 | `lib/PC_Logger`、`lib/PC_Util/LogStats.h`、`logstat`、`tools/analyze_logs.py` |
