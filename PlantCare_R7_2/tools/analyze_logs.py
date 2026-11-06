#!/usr/bin/env python3
r"""把串口导出的日志整理成报告用的数据 (只用 Python 标准库)。

输入: 打开串口时 logs/ 文件夹自动保存的记录 (或你复制的任何文本), 里面先后输入过
      dump (采样与事件 CSV) 和 aidump (AI 解释 JSON 行)。行首的 "14:05:12.345 > " 时间前缀会自动去掉,
      同一条记录出现多次 (多次 dump) 会自动去重。

用法 (Mac):
  python3 tools/analyze_logs.py logs/*.log --out report_12h --start "2026-11-04 23:00"
用法 (Windows, 用 PlatformIO 自带的 Python, 不用另装):
  "%USERPROFILE%\.platformio\penv\Scripts\python.exe" tools\analyze_logs.py logs\*.log --out report_12h --start "2026-11-04 23:00"
--start 填输入 clear 的时间: 它之前的记录 (比如 logs/ 里台架测试时导出的旧记录) 都不计入。
不填就从第一条采样算起, 这时只能交给它 clear 之后导出的记录。
连续运行窗口默认 12 h (方案第 7 章, 11 月 4 日由 24 h 改为 12 h): 按 5 min 一条应有 144 条, 有效 >= 137 条 (95 %)。
别的时长加 --hours, 例如 --hours 24 (288 条中 >= 274 条)。

输出 (屏幕 + --out 文件夹):
  summary.md     连续运行指标 (窗口内有效条数与判定)、复位原因、状态变化时间线、
                 AI 解释调用成功率 / 回退率 / 延迟 p50 p95 (按 HTTP 请求统计)
  log.csv        干净的采样+事件表 (可直接用 Excel 打开)
  ai.jsonl       AI 解释原始记录
  ai_review.csv  AI 解释逐条核对表: 两名成员分别填写 "输入之外的数字或断言" 和 "与当前状态矛盾"
"""
import argparse
import csv
import datetime as dt
import glob
import json
import math
import os
import re
import sys

PREFIX = re.compile(r"^\s*\d{2}:\d{2}:\d{2}\.\d{3} > ")
ROW = re.compile(r"^\d+,\d+,\d+,[SE],")
HEADER = ["epoch", "boot", "ms", "type", "state", "mv", "R", "valid", "motion", "detail"]
BAD_RESETS = {"BROWNOUT", "PANIC", "INT_WDT", "TASK_WDT", "WDT"}

try:
    from zoneinfo import ZoneInfo
except ImportError:  # Python < 3.9
    ZoneInfo = None


def expand(paths):
    # Windows 的命令行不会展开 logs\*.log, 这里自己展开
    out = []
    for p in paths:
        hits = sorted(glob.glob(p)) if any(ch in p for ch in "*?[") else [p]
        out.extend(hits)
    return out


def read_inputs(paths):
    rows, ai, seen_rows, seen_ai = [], [], set(), set()
    for p in expand(paths):
        with open(p, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = PREFIX.sub("", raw.rstrip("\r\n")).strip()
                if ROW.match(line):
                    if line not in seen_rows:
                        seen_rows.add(line)
                        rows.append(line.split(",", 9))
                elif line.startswith('{"t":'):
                    if line not in seen_ai:
                        try:
                            ai.append(json.loads(line))
                            seen_ai.add(line)
                        except ValueError:
                            pass
    for r in rows:
        while len(r) < 10:
            r.append("")
    rows.sort(key=lambda r: (int(r[1]), int(r[2])))          # 开机编号 + 开机毫秒 = 先后顺序
    ai.sort(key=lambda a: (a.get("boot", 0), a.get("id", 0)))
    return rows, ai


def since_start(rows, ai, start_epoch):
    """--start 之前的记录不算 (logs/ 里可能还有台架测试时导出的旧记录)。
    运行从 --start 之后第一条带时间的记录所在的那次开机算起; 之后重启、还没校时的记录 (时间为 0) 也算在内。"""
    timed = [int(r[1]) for r in rows if int(r[0]) >= start_epoch]
    if not timed:
        return [], [], len(rows), len(ai)
    b0 = min(timed)
    keep = [r for r in rows if int(r[0]) >= start_epoch or (int(r[0]) == 0 and int(r[1]) > b0)]
    keep_ai = [x for x in ai if (x.get("t") or 0) >= start_epoch or (not x.get("t") and (x.get("boot") or 0) > b0)]
    return keep, keep_ai, len(rows) - len(keep), len(ai) - len(keep_ai)


def fmt_time(epoch, tz):
    if not epoch:
        return "time unknown"
    t = dt.datetime.fromtimestamp(epoch, tz) if tz else dt.datetime.fromtimestamp(epoch)
    return t.strftime("%m-%d %H:%M")


def pct(values, q):
    if not values:
        return None
    s = sorted(values)
    return s[max(0, math.ceil(q * len(s)) - 1)]


def sample_stats(rows, period_s, window_s, need_valid, start_epoch):
    s = [r for r in rows if r[3] == "S"]
    out = {"rows": len(s), "valid": sum(r[7] == "1" for r in s),
           "flagged": sum(("FAST" in r[9] or "SIM" in r[9]) for r in s)}
    if len(s) < 2:
        return out
    eps = [int(r[0]) for r in s]
    boots = {r[1] for r in s}
    if all(eps):
        axis, t = "NTP clock", eps
    elif len(boots) == 1:
        axis, t = "uptime of one boot", [int(r[2]) / 1000.0 for r in s]
    else:
        out["axis"] = None
        return out
    out["axis"] = axis
    out["span_h"] = (t[-1] - t[0]) / 3600.0
    out["expected"] = (t[-1] - t[0]) / period_s + 1
    gaps = [b - a for a, b in zip(t, t[1:])]
    out["max_gap_min"] = max(gaps) / 60.0 if gaps else 0
    t0 = t[0]
    if start_epoch and axis == "NTP clock":
        t0 = start_epoch
    win = [(ti, r) for ti, r in zip(t, s) if t0 <= ti < t0 + window_s]
    out["win_rows"] = len(win)
    out["win_valid"] = sum(r[7] == "1" for _, r in win)
    out["win_complete"] = t[-1] + period_s >= t0 + window_s
    out["win_pass"] = out["win_complete"] and out["win_valid"] >= need_valid
    out["win_start"] = t0
    return out


def event_stats(rows):
    boots, resets, states = 0, {}, []
    for r in rows:
        if r[3] != "E":
            continue
        d = r[9]
        if d.startswith("boot "):
            boots += 1
            m = re.search(r"reset=(\S+)", d)
            reason = m.group(1) if m else "?"
            resets[reason] = resets.get(reason, 0) + 1
        elif d.startswith("state "):
            states.append((int(r[0]), r[1], int(r[2]), d[6:]))
    return boots, resets, states


def ai_stats(ai):
    res = {"results": len(ai), "ai": sum(a.get("source") == "AI" for a in ai)}
    res["tpl"] = res["results"] - res["ai"]
    called = [a for a in ai if a.get("attempts", 0) > 0]
    res["called"] = len(called)
    res["called_ok"] = sum(a.get("source") == "AI" for a in called)
    lat = [a.get("latency_ms", 0) for a in called]
    res["p50"], res["p95"] = pct(lat, 0.50), pct(lat, 0.95)
    res["retries"] = sum(a.get("attempts", 0) > 1 for a in called)
    reasons = {}
    for a in ai:
        if a.get("source") != "AI":
            key = str(a.get("reason", "?")).split(":")[0]
            reasons[key] = reasons.get(key, 0) + 1
    res["reasons"] = reasons
    by_event = {}
    for a in ai:
        e = a.get("event", "?")
        ok, total = by_event.get(e, (0, 0))
        by_event[e] = (ok + (a.get("source") == "AI"), total + 1)
    res["by_event"] = by_event
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--out", help="folder for summary.md, log.csv, ai.jsonl, ai_review.csv")
    ap.add_argument("--hours", type=float, default=12.0,
                    help="continuous-run window in hours (plan ch.7: 12); pass needs >= 95%% of its rows valid")
    ap.add_argument("--start", help='window start, local time "YYYY-MM-DD HH:MM" (default: first sample row); '
                                    'records before it are ignored')
    ap.add_argument("--tz", default="Australia/Sydney")
    ap.add_argument("--period", type=int, default=300, help="sample row period in s (firmware: 300)")
    a = ap.parse_args()
    if a.period <= 0 or a.hours * 3600 < a.period:
        sys.exit("--hours must cover at least one sample period")
    window_s = int(round(a.hours * 3600))
    win_expected = window_s // a.period            # 12 h: 144 rows; 24 h: 288 rows
    need_valid = (win_expected * 95 + 99) // 100   # >= 95 %, rounded up: 12 h -> 137, 24 h -> 274
    win_name = "%g h" % a.hours

    tz = None
    if ZoneInfo:
        try:
            tz = ZoneInfo(a.tz)
        except Exception:  # Windows 没有时区数据库 (未装 tzdata): 用电脑本地时间
            tz = None
    start_epoch = None
    if a.start:
        st = dt.datetime.strptime(a.start, "%Y-%m-%d %H:%M")
        start_epoch = int((st.replace(tzinfo=tz) if tz else st).timestamp())

    rows, ai = read_inputs(a.files)
    if not rows and not ai:
        sys.exit("no log rows found: in the serial monitor type  dump  and  aidump  first")
    dropped = None
    if start_epoch:
        rows, ai, n_rows, n_ai = since_start(rows, ai, start_epoch)
        dropped = (n_rows, n_ai)
    ss = sample_stats(rows, a.period, window_s, need_valid, start_epoch)
    boots, resets, states = event_stats(rows)
    ais = ai_stats(ai)

    L = ["# PlantCare R7.2 log summary", ""]
    L.append("## %s continuous run (plan ch.7: >= %d of %d valid rows, no abnormal reboot)" % (
        win_name, need_valid, win_expected))
    if dropped is not None:
        L.append("- --start %s: earlier records ignored (%d log rows, %d AI records)%s" % (
            a.start, dropped[0], dropped[1], "" if rows else "; nothing at or after this time: check the date"))
    L.append("- sample rows: %d, valid: %d, FAST/SIM rows: %d%s" % (ss["rows"], ss["valid"], ss["flagged"],
             " (bench rows: not normal parameters)" if ss["flagged"] else ""))
    if ss.get("axis"):
        L.append("- time axis: %s, span %.2f h, expected %.0f rows, coverage %.1f %% (valid %.1f %%)" % (
            ss["axis"], ss["span_h"], ss["expected"], 100.0 * ss["rows"] / ss["expected"],
            100.0 * ss["valid"] / ss["expected"]))
        L.append("- longest gap between sample rows: %.1f min" % ss["max_gap_min"])
        if ss["win_complete"]:
            L.append("- %s window from %s: %d rows, %d valid (need >= %d) -> **%s**" % (
                win_name, fmt_time(ss["win_start"], tz) if ss["axis"] == "NTP clock" else "first row",
                ss["win_rows"], ss["win_valid"], need_valid, "PASS" if ss["win_pass"] else "FAIL"))
        else:
            L.append("- %s window: not complete yet (%.2f h of data)" % (win_name, ss["span_h"]))
    elif ss["rows"] >= 2:
        L.append("- span unknown: rows without NTP time across several boots")
    bad = sum(n for k, n in resets.items() if k in BAD_RESETS)
    L.append("- boots logged: %d (%s), abnormal resets: %d" % (
        boots, ", ".join("%s x%d" % kv for kv in resets.items()) or "none", bad))
    L.append("")
    L.append("## State changes (%d)" % len(states))
    for ep, boot, ms, d in states:
        L.append("- %s  %s" % (fmt_time(ep, tz) if ep else "boot%s+%ds" % (boot, ms // 1000), d))
    L.append("")
    L.append("## AI explanations")
    L.append("- results: %d (AI %d, template %d)" % (ais["results"], ais["ai"], ais["tpl"]))
    if ais["called"]:
        L.append("- with an HTTP call: %d, AI shown %d -> success %.1f %%, fallback %.1f %%, retried %d" % (
            ais["called"], ais["called_ok"], 100.0 * ais["called_ok"] / ais["called"],
            100.0 - 100.0 * ais["called_ok"] / ais["called"], ais["retries"]))
        L.append("- latency of the last HTTP attempt: p50 %d ms, p95 %d ms" % (ais["p50"], ais["p95"]))
    if ais["reasons"]:
        L.append("- template reasons: " + ", ".join("%s x%d" % kv for kv in sorted(ais["reasons"].items())))
    if ais["by_event"]:
        L.append("- by event (AI/total): " + ", ".join("%s %d/%d" % (e, ok, n) for e, (ok, n) in sorted(ais["by_event"].items())))
    text = "\n".join(L) + "\n"
    print(text)

    if a.out:
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8") as f:
            f.write(text)
        with open(os.path.join(a.out, "log.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(HEADER + ["local_time"])
            for r in rows:
                w.writerow(r + [fmt_time(int(r[0]), tz) if int(r[0]) else ""])
        with open(os.path.join(a.out, "ai.jsonl"), "w", encoding="utf-8") as f:
            for x in ai:
                f.write(json.dumps(x, ensure_ascii=False) + "\n")
        with open(os.path.join(a.out, "ai_review.csv"), "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["time", "event", "state", "source", "reason", "latency_ms", "headline", "body", "input",
                        "A_extra_number_or_claim(Y/N)", "A_contradicts_state(Y/N)",
                        "B_extra_number_or_claim(Y/N)", "B_contradicts_state(Y/N)", "notes"])
            for x in ai:
                w.writerow([fmt_time(x.get("t", 0), tz), x.get("event"), x.get("state"), x.get("source"),
                            x.get("reason"), x.get("latency_ms"), x.get("headline"), x.get("body"),
                            json.dumps(x.get("input"), ensure_ascii=False), "", "", "", "", ""])
        print("written: %s/{summary.md, log.csv, ai.jsonl, ai_review.csv}" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
