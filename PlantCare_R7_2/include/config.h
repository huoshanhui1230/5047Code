#pragma once
// PlantCare R7.2 计划参数 (对应方案 R7.2 第 4、6 章)。改参数只改这里。

#define FW_VERSION              "R7.2-fw1.4"
#define NVS_NAMESPACE           "pc72"      // 与旧固件分开, 避免读到旧设置

// ---------- 采样与有效性 (第 6 章 计划参数) ----------
#define SAMPLE_PERIOD_MS        60000UL     // 每 60 s 一轮
#define SAMPLE_PERIOD_FAST_MS    5000UL     // fast on: 台架快速参数, 屏幕标 FAST
#define SAMPLES_PER_ROUND           7       // 每轮 7 次取中位数
#define SAMPLE_GAP_MS              15       // 同一轮内两次 ADC 之间的间隔
#define CONFIRM_ROUNDS              3       // 连续 3 轮有效读数才转换状态
#define VALID_MIN_MV              200       // ESP32 ADC (11 dB) 能测的最低值约 142 mV: 探头断开、没供电或湿到超出量程时
                                            // 读数都停在这个底值 (极差 0)。低于 200 mV 一律视为无效, 不能当成"很湿"
#define VALID_MAX_MV             3000       // 高于此值视为异常 (SEN0308 标称最高约 2.9 V)
#define ROUND_SPREAD_MAX_MV       150       // 一轮 7 次读数极差上限, 超过视为接触不良/悬空
#define INVALID_ROUNDS_TO_PAUSE     3       // 连续 3 轮无效 -> 设备暂停判断
#define UNCONFIRMED_ROUNDS_TO_PAUSE 15      // fw1.4: 植物状态下 15 轮都凑不齐"连续 3 轮有效" (有效、无效交替) -> 暂停判断,
                                            // 连续 3 轮有效才恢复。按 24 h 记录里无效读数的实际分布调整; 0 = 不检查
#define CAL_MIN_SPAN_MV           300       // |湿端-干端| 小于此值 = 标定失败

// ---------- 等待水渗透 ----------
#define WAIT_MIN_DEFAULT           10       // 分钟, 按实测改 (set wait)
#define WAIT_FAST_MS            60000UL     // fast on 时等待 1 分钟

// ---------- 语音 ----------
#define VOICE_VOLUME_DEFAULT       11       // DFPlayer 0-30; 10-29 在 t7 里试听确认 11 合适 (R7_Main 记录)
// 你们的 DFPlayer 能收命令、能放音, 但从不回任何字节 (R7_Main 记录: tracks=-1, events=0)。
// 1 = 模块没有回音时照样发送播放命令 ("只发不收"), 不能自动发现缺卡或模块故障, 要靠试听确认;
// 0 = 收不到回音就当作语音离线 (不出声)。
#define VOICE_TX_ONLY_FALLBACK      1
#define VOICE_COOLDOWN_MS     600000UL      // talk normal: 缺水那句走近时至少间隔 10 min
#define VOICE_DAILY_MAX             6       // talk normal: 每天主动播报上限 (fw1.3 起只有缺水计入)
// fw1.3 走近播报规则 (talk normal), 见 lib/PC_Care/VoicePolicy.h
#define VOICE_WAIT_AGAIN_DIV        2       // 等待中走近: 距上一次等待语音过了 等待时长/2 才再说 (默认 10 min 等待时为 5 min)
#define VOICE_RECHECK_QUIET_WAITS   1       // 等待后未确认恢复: 再静默几个等待时长才播缺水那句
#define VOICE_TEST_GAP_MS        8000UL     // talk test: 两次之间至少 8 s
#define VOICE_USE_MP3_FOLDER        1       // 1: /mp3/0001..0003 (playMp3Folder); 0: 按卡内顺序 play()
// TF 卡 /mp3 里三段语音的编号 (文件名前 4 位, 后面可以带名字, 如 0001_thirsty.mp3)。
// 现在的卡 (R7_Main 用的): 0001_thirsty = 缺水, 0002_enough_water = 不缺水, 0003_soaking_in = 等待。
// 方案第 2 章的表写的是 0002 = 等待、0003 = 不缺水: 以卡上实际文件为准, 两者只改一处即可。
#define CLIP_DRY                    1
#define CLIP_OK                     2
#define CLIP_WAIT                   3
#define QUIET_START_MIN   (21 * 60)         // 安静时段 21:00
#define QUIET_END_MIN     (7 * 60 + 30)     //          至 07:30
#define TZ_SYDNEY  "AEST-10AEDT,M10.1.0,M4.1.0/3"

// ---------- 走近 ----------
#define PIR_WARMUP_MS           60000UL     // 上电 60 s 内忽略 PIR
#define PIR_PIN_MODE              INPUT     // 与 R7_Main 里测试通过的 t4 相同 (不加内部下拉)
#define PIR_STUCK_MS           300000UL     // 持续高电平 5 min 视为 PIR 故障
#define INTERACTION_WINDOW_MS   60000UL     // 最后一次移动后 60 s 交互窗口

// ---------- 按钮 ----------
#define BUTTON_DEBOUNCE_MS         40

// ---------- 显示与灯光 ----------
#define RING_BRIGHTNESS            40       // 灯环亮度上限 40/255
#define BOARD_RGB_BRIGHTNESS       20
#define OLED_CONTRAST             160
#define OLED_SHIFT_PERIOD_MS   600000UL     // 每 10 min 平移 1 像素防烧屏

// ---------- 日志 ----------
#define LOG_PERIOD_MS          300000UL     // 每 5 min 一条采样记录
#define LOG_MAX_BYTES         (128 * 1024)  // 单个日志文件上限, 超过轮换 (保留 2 个, 约一周); 分区 896 KB
#define AI_LOG_MAX_BYTES       (64 * 1024)

// ---------- 开机沿用原状态 ----------
#define PREV_STATE_TRUST_S      (6UL * 3600UL)  // 断电前状态在 6 h 内且时间已校准 -> 可信

// ---------- AI 运行时解释 (第 4 章) ----------
#define AI_DEFAULT_MODEL        "gemini-3.5-flash-lite"
#define AI_HOST_PATH_FMT        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"
#define AI_TIMEOUT_MS            8000       // 单次超时 8 s
#define AI_MIN_INTERVAL_MS      60000UL     // 自动调用最短间隔 60 s
#define AI_MANUAL_MIN_INTERVAL_MS 10000UL   // explain 命令最短间隔
#define AI_DAILY_MAX               50       // 每日上限
#define AI_DELAY_AFTER_EVENT_MS  5000UL     // 状态变化后等语音播完再调用
#define AI_MIN_FREE_HEAP        60000       // 剩余内存不足时直接用模板
#define AI_MIN_BLOCK_HEAP       24000       // TLS 需要一块连续内存, 最大可分配块不足时也用模板
// 请求体 (temperature 0.2, maxOutputTokens 1024, JSON 输出格式) 在 lib/PC_Explain/Explain.cpp 的 buildRequestBody()
#define AI_PROMPT_VERSION       "P3"   // P3 (fw1.4): 等待后恢复/未恢复按"最近连续 3 轮"描述, 输入加 consecutive_at_or_above_H;
                                      //   dry_to_ok 不等于有人补水; 数字照抄。P2: 植物改为花烛, 不再写 Australian native plant
#define AI_SCHEMA_VERSION       "S1"
#define AI_HEADLINE_MAX            20
#define AI_BODY_MAX               100

// ---------- 档案摘要 (送给 AI 的植物信息; 改动时在串口输入 ver profile 记下新版本) ----------
// P2 (2026-11-01): 展示植物改为花烛 Anthurium crystallinum。依据 RHS 植物档案 (Moisture: "Moist but well-drained")。
// 措辞避开本地校验的禁用词 (light、sun、root 等), 也不写数字, 免得 AI 照抄后校验不过。
#define PROFILE_PLANT     "Anthurium crystallinum (crystal anthurium)"
#define PROFILE_WATER     "likes moist but well-drained mix"
#define PROFILE_SOURCE    "RHS plant profile: Anthurium crystallinum (accessed 2026-11)"
