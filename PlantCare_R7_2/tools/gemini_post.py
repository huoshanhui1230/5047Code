#!/usr/bin/env python3
r"""在电脑上用和 ESP32 完全相同的请求体调用 Gemini, 用来调提示词、看原始返回 (只用 Python 标准库)。

用法:
  1. 烧录 t10_ai, 串口输入  body ok_to_dry  , 把打印出来的那一行 JSON 复制存成 body.json
     (行首带 "14:05:12.345 > " 时间前缀也没关系, 脚本会去掉)
  2. 终端里 (密钥只放环境变量, 不写进文件, 不提交):
       Mac:         export GEMINI_API_KEY=你的密钥
                    python3 tools/gemini_post.py body.json
       Windows cmd: set GEMINI_API_KEY=你的密钥
                    "%USERPROFILE%\.platformio\penv\Scripts\python.exe" tools\gemini_post.py body.json
       (PowerShell 里设密钥: $env:GEMINI_API_KEY="你的密钥")
       换模型或看原始返回: 加 --model gemini-3.1-flash-lite --raw
输出: HTTP 状态、耗时、模型版本、结束原因、模型返回的 headline/body, 以及长度和 ASCII 的快速检查。
完整校验 (数字是否来自输入、禁用词、与状态矛盾) 以设备上的 t10_ai / app 为准。
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

URL = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
PREFIX = re.compile(r"^\s*\d{2}:\d{2}:\d{2}\.\d{3} > ")


def load_body(path):
    with open(path, encoding="utf-8") as f:
        lines = [PREFIX.sub("", ln.rstrip("\r\n")) for ln in f]
    dec = json.JSONDecoder()
    # 优先用 t10 打印的完整那一行
    for ln in lines:
        s = ln.strip()
        if s.startswith('{"systemInstruction"'):
            try:
                return dec.raw_decode(s)[0]
            except ValueError:
                break  # 这一行被折断了, 下面把各行接起来再试
    text = "".join(lines)
    start = text.find('{"systemInstruction"')
    if start < 0:
        start = text.find("{")
    if start < 0:
        sys.exit("no JSON object found in " + path)
    try:
        return dec.raw_decode(text[start:])[0]
    except ValueError as e:
        sys.exit("could not read the request body (%s): copy the whole line printed by  body <event>" % e)


def model_text(resp):
    cands = resp.get("candidates") or []
    if not cands:
        return None, resp.get("promptFeedback", {}).get("blockReason")
    parts = cands[0].get("content", {}).get("parts", [])
    text = "".join(p.get("text", "") for p in parts if not p.get("thought"))
    return text, cands[0].get("finishReason")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("body", help="request body JSON (from t10_ai: body <event>)")
    ap.add_argument("--model", default="gemini-3.5-flash-lite")
    ap.add_argument("--raw", action="store_true", help="also print the raw response JSON")
    ap.add_argument("--timeout", type=float, default=20)
    a = ap.parse_args()

    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not key:
        sys.exit("set GEMINI_API_KEY first (export GEMINI_API_KEY=...)")
    body = load_body(a.body)
    req = urllib.request.Request(
        URL.format(model=a.model),
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json", "x-goog-api-key": key},
        method="POST",
    )
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=a.timeout) as r:
            status, raw = r.status, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read().decode("utf-8", "replace")
    except urllib.error.URLError as e:
        sys.exit("network error: %s" % e.reason)
    ms = (time.time() - t0) * 1000
    print("HTTP %d, %.0f ms, model %s" % (status, ms, a.model))
    try:
        resp = json.loads(raw)
    except ValueError:
        print(raw[:2000])
        return 1
    if a.raw:
        print(json.dumps(resp, indent=2, ensure_ascii=False))
    if status != 200:
        print("error:", resp.get("error", {}).get("message", raw[:500]))
        return 1
    text, finish = model_text(resp)
    print("modelVersion:", resp.get("modelVersion"), "| finish:", finish)
    usage = resp.get("usageMetadata", {})
    if usage:
        print("tokens: prompt %s, output %s, thoughts %s" % (usage.get("promptTokenCount"),
              usage.get("candidatesTokenCount"), usage.get("thoughtsTokenCount", 0)))
    if text is None:
        print("no candidate returned")
        return 1
    print("text:", text)
    try:
        out = json.loads(re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip()))
    except ValueError:
        print("CHECK: model text is not JSON -> device would retry once, then use the template")
        return 1
    h, b = str(out.get("headline", "")).strip(), str(out.get("body", "")).strip()
    print("headline (%d chars, max 20): %s" % (len(h), h))
    print("body     (%d chars, max 100): %s" % (len(b), b))
    problems = []
    if not h or len(h) > 20:
        problems.append("headline length")
    if not b or len(b) > 100:
        problems.append("body length")
    if any(ord(ch) > 126 or ord(ch) < 32 for ch in h + b):
        problems.append("non-ASCII character (OLED font is ASCII)")
    print("quick check:", "ok" if not problems else "PROBLEM: " + ", ".join(problems))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
