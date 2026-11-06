#!/bin/bash
# PlantCare R7.2: 双击运行, 选编号 -> 编译 + 烧录 + 打开串口 (串口里 Ctrl+C 退出, 回到菜单)
# 第一次双击若提示"无法打开", 在 Finder 里右键 -> 打开; 或终端执行: chmod +x 烧录.command
cd "$(dirname "$0")" || exit 1
PIO="$(command -v pio 2>/dev/null)"
if [ -z "$PIO" ] && [ -x "$HOME/.platformio/penv/bin/pio" ]; then PIO="$HOME/.platformio/penv/bin/pio"; fi
if [ -z "$PIO" ]; then
  echo "没找到 PlatformIO (pio)。先在 VS Code 里安装 PlatformIO 插件。"
  read -r -p "按回车关闭" _
  exit 1
fi
ENVS=(t1_i2c_scan t2_oled t3_probe t4_pir t5_button t6_ring t7_dfplayer t8_power t9_selftest t10_ai app app_noTF)
DESC=("OLED 是否连上 (0x3C)" "屏幕三种表情和解释页" "探头读数 / 三级标定" "PIR 走近触发" "按钮计数" "灯环与板载灯颜色"
      "三句语音" "供电压力 10 min (灯环+语音+Wi-Fi+AI)" "全部模块自检" "Wi-Fi + Gemini 解释"
      "整机主程序 (有 TF 卡)" "整机主程序 (无 TF 卡, 台词打印在串口和屏幕)")
while true; do
  echo
  echo "======== PlantCare R7.2 烧录 ========"
  for i in "${!ENVS[@]}"; do printf "%3d) %-12s %s\n" $((i + 1)) "${ENVS[$i]}" "${DESC[$i]}"; done
  echo "  m) 只打开串口 (不烧录)"
  echo "  c) 全部编译一遍 (不烧录, 第一次拿到代码时做)"
  echo "  e) 擦除整块闪存 (只在 app 反复重启时用; 之后要重新输入 wifi / wifi2 / apikey)"
  echo "  q) 退出"
  read -r -p "选择: " n
  case "$n" in
    q|Q) exit 0 ;;
    m|M) "$PIO" device monitor -e app ;;
    e|E)
      read -r -p "擦除板子上的全部闪存 (设置和日志都会清空)? 输入 YES 确认: " yn
      if [ "$yn" = "YES" ]; then "$PIO" run -e app -t erase && echo "完成。再选 11 烧录 app, 然后重新输入 wifi / wifi2 / apikey。"; fi
      ;;
    c|C)
      args=()
      for e in "${ENVS[@]}"; do args+=(-e "$e"); done
      "$PIO" run "${args[@]}"
      ;;
    *)
      if [[ "$n" =~ ^[0-9]+$ ]] && [ "$n" -ge 1 ] && [ "$n" -le ${#ENVS[@]} ]; then
        e="${ENVS[$((n - 1))]}"
        echo "---- $e: 编译并烧录; 卡在 Connecting... 时按住 BOOT 再点一下 RST ----"
        "$PIO" run -e "$e" -t upload && "$PIO" device monitor -e "$e"
      else
        echo "无效选择"
      fi
      ;;
  esac
done
