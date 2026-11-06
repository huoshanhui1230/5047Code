#include "Explain.h"
#include "MiniJson.h"
#include <cstdio>
#include <cctype>
#include <set>
#include <algorithm>

const char* const kExplainSystemPrompt =
    "You write one short explanation for a soil-moisture care device that sits beside one potted "
    "plant. The device measures a pot-specific relative wetness index R "
    "(0-100, this pot and probe only). L is the check line and H is the recovery line. "
    "Local rules decide the care state; you only explain it. Use ONLY the facts in the input JSON.\n"
    "Rules:\n"
    "1. Plain English for a novice carer. Talk about the soil reading, not the plant's feelings.\n"
    "2. Every number you write must appear in the input JSON, copied exactly as written. Do not calculate "
    "new numbers. No percentages.\n"
    "3. Do not diagnose causes. Never mention roots, rot, disease, pests, fungus, waterlogging, sun, "
    "light, heat, rain or weather.\n"
    "4. If state is OK or WAITING, do not ask the carer to water. If state is DRY, ask them to check "
    "the soil, not to water automatically.\n"
    "5. headline: at most 20 characters. body: at most 100 characters, one or two short sentences. "
    "Plain ASCII only.\n"
    "6. Say R is below or above a line only when the input numbers show it.\n"
    "Event meanings: first_state = device just started; ok_to_dry = R stayed below L for "
    "confirm_rounds readings; dry_to_wait = the carer pressed the watered button; wait_to_ok = after "
    "the wait the last confirm_rounds readings were all at or above H; wait_to_dry = after the wait, "
    "recovery is not confirmed: the last confirm_rounds readings were not all at or above H (R itself "
    "may already be at or above H; consecutive_at_or_above_H says how many in a row were); dry_to_ok = R "
    "stayed at or above H for confirm_rounds readings without a button press, which alone does not show "
    "that anyone watered; manual = explain the current state.\n"
    "Return JSON only, matching the schema.";

const char* explainEventName(ExplainEvent e) {
  switch (e) {
    case ExplainEvent::FirstState: return "first_state";
    case ExplainEvent::OkToDry:    return "ok_to_dry";
    case ExplainEvent::DryToWait:  return "dry_to_wait";
    case ExplainEvent::WaitToOk:   return "wait_to_ok";
    case ExplainEvent::WaitToDry:  return "wait_to_dry";
    case ExplainEvent::DryToOk:    return "dry_to_ok";
    case ExplainEvent::Manual:     return "manual";
  }
  return "manual";
}

// ---------- number formatting ----------
namespace {

std::string trimZeros(std::string s) {
  if (s.find('.') == std::string::npos) return s;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  return s;
}

std::string fmtFixed(float v, int decimals) {
  if (std::isnan(v)) return "null";
  char buf[32];
  if (decimals <= 0) {
    snprintf(buf, sizeof(buf), "%ld", static_cast<long>(std::lround(v)));
    return buf;
  }
  snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
  return trimZeros(buf);
}

std::string fmtR(float v) { return fmtFixed(v, 0); }
// fw1.4: R 离 L 或 H 不到 0.5 时取一位小数并向下截断, 显示的大小关系与状态机的比较一致
// (例如 R = 43.97 < L = 44 显示 43.9, 不会出现 "R 44 is below 44")。
std::string fmtRvs(float v, float L, float H) {
  if (std::isnan(v)) return "null";
  const bool nearL = !std::isnan(L) && std::fabs(v - L) < 0.5f;
  const bool nearH = !std::isnan(H) && std::fabs(v - H) < 0.5f;
  if (!nearL && !nearH) return fmtR(v);
  return fmtFixed(static_cast<float>(std::floor(static_cast<double>(v) * 10.0) / 10.0), 1);
}
std::string fmtLH(float v) {
  if (std::isnan(v)) return "null";
  return (std::fabs(v - std::round(v)) < 0.05f) ? fmtFixed(v, 0) : fmtFixed(v, 1);
}
std::string fmtHours(float v) {
  if (std::isnan(v)) return "null";
  return (v < 10.0f) ? fmtFixed(v, 1) : fmtFixed(v, 0);
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

std::string lower(const std::string& s) {
  std::string o(s);
  for (char& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return o;
}

// 提取文本中的数字 (忽略符号), 统一去掉末尾 0
std::vector<std::string> numbersIn(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) {
      size_t j = i;
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) j++;
      if (j + 1 < s.size() && s[j] == '.' && std::isdigit(static_cast<unsigned char>(s[j + 1]))) {
        j++;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) j++;
      }
      std::string tok = s.substr(i, j - i);
      // 去掉前导 0 (保留单个 0 和 0.x)
      size_t k = 0;
      while (k + 1 < tok.size() && tok[k] == '0' && tok[k + 1] != '.') k++;
      out.push_back(trimZeros(tok.substr(k)));
      i = j;
    } else {
      i++;
    }
  }
  return out;
}

std::set<std::string> allowedNumbers(const std::string& contextJson) {
  std::set<std::string> ok;
  for (const auto& n : numbersIn(contextJson)) {
    ok.insert(n);
    if (n.find('.') != std::string::npos) {
      double v = std::strtod(n.c_str(), nullptr);
      char buf[24];
      snprintf(buf, sizeof(buf), "%ld", static_cast<long>(std::floor(v))); ok.insert(buf);
      snprintf(buf, sizeof(buf), "%ld", static_cast<long>(std::ceil(v)));  ok.insert(buf);
      snprintf(buf, sizeof(buf), "%ld", static_cast<long>(std::lround(v))); ok.insert(buf);
    }
  }
  return ok;
}

std::vector<std::string> wordsIn(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (std::isalpha(static_cast<unsigned char>(ch))) {
      cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    } else if (!cur.empty()) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

const char* const kBannedExact[] = {
    "rot", "rots", "rotting", "rotten", "sun", "sunny", "sunlight", "light", "lights", "heat", "hot",
    "rain", "rained", "raining", "weather", "pest", "pests", "feel", "feels", "feeling", "happy", "sad",
    "dead", "die", "dies", "dying", "sick"};
const char* const kBannedPrefix[] = {"root", "waterlog", "diseas", "fung", "thirst", "wilt",
                                     "drown", "mould", "mold", "overwater"};
const char* const kAskWater[] = {"water now", "water it", "water the", "needs water", "need water",
                                 "needs watering", "please water", "time to water", "water again",
                                 "add water", "add more water", "give it water", "water more"};
const char* const kNoNeed[] = {"no water needed", "no need to water", "does not need water",
                               "doesn't need water", "no watering needed", "don't water", "do not water"};

}  // namespace

// ---------- context & request ----------
std::string buildContextJson(const ExplainContext& c) {
  std::string j = "{";
  j += "\"event\":" + mj::quote(explainEventName(c.event));
  j += ",\"state\":" + mj::quote(c.state);
  j += ",\"R\":" + fmtRvs(c.R, c.L, c.H);
  j += ",\"L\":" + fmtLH(c.L);
  j += ",\"H\":" + fmtLH(c.H);
  j += ",\"scale\":\"R 0-100, this pot only\"";
  j += ",\"confirm_rounds\":" + std::to_string(c.confirmRounds);
  j += ",\"consecutive_at_or_above_H\":" + (c.aboveHRounds < 0 ? std::string("null") : std::to_string(c.aboveHRounds));
  j += ",\"mode\":" + mj::quote(c.mode);
  j += ",\"local_time\":" + (c.localTime.empty() ? std::string("null") : mj::quote(c.localTime));
  j += ",\"hours_since_water\":" + fmtHours(c.hoursSinceWater);
  j += ",\"R_before_water\":" + fmtR(c.rBeforeWater);
  j += ",\"wait_min\":" + std::to_string(c.waitMin);
  j += ",\"R_at_check\":" + fmtRvs(c.rAtCheck, c.L, c.H);
  j += ",\"R_drop_per_hour_12h\":" + fmtFixed(c.dropPerHour12h, 1);
  j += ",\"prev_cycle_hours\":" + fmtFixed(c.prevCycleHours, 0);
  j += ",\"hourly_R\":[";
  for (size_t i = 0; i < c.hourlyR.size(); i++) {
    if (i) j += ",";
    j += std::to_string(c.hourlyR[i]);
  }
  j += "]";
  j += ",\"plant\":" + mj::quote(c.plant);
  j += ",\"water_need\":" + mj::quote(c.waterNeed);
  j += ",\"source\":" + mj::quote(c.source);
  j += "}";
  return j;
}

std::string buildRequestBody(const std::string& contextJson, const std::string& retryHint) {
  std::string user = "Input JSON:\n" + contextJson + "\nWrite the explanation.";
  if (!retryHint.empty()) user += "\n" + retryHint;
  std::string b = "{";
  b += "\"systemInstruction\":{\"parts\":[{\"text\":" + mj::quote(kExplainSystemPrompt) + "}]}";
  b += ",\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":" + mj::quote(user) + "}]}]";
  b += ",\"generationConfig\":{\"temperature\":0.2,\"maxOutputTokens\":1024,"
       "\"responseMimeType\":\"application/json\","
       "\"responseSchema\":{\"type\":\"OBJECT\",\"properties\":{"
       "\"headline\":{\"type\":\"STRING\"},"
       "\"body\":{\"type\":\"STRING\"},"
       "\"cited_fields\":{\"type\":\"ARRAY\",\"items\":{\"type\":\"STRING\"}}},"
       "\"required\":[\"headline\",\"body\"],"
       "\"propertyOrdering\":[\"headline\",\"body\",\"cited_fields\"]}}";
  b += "}";
  return b;
}

// ---------- response parsing ----------
std::string apiErrorMessage(const std::string& httpBody) {
  mj::Value root;
  std::string err;
  if (!mj::parse(httpBody, root, err)) return "";
  const mj::Value* e = root.get("error");
  if (!e) return "";
  const mj::Value* m = e->get("message");
  return (m && m->isStr()) ? m->str : "error";
}

bool extractModelText(const std::string& httpBody, std::string& text, std::string& modelVersion, std::string& err) {
  text.clear();
  modelVersion.clear();
  mj::Value root;
  std::string perr;
  if (!mj::parse(httpBody, root, perr)) { err = "response not JSON (" + perr + ")"; return false; }
  if (const mj::Value* e = root.get("error")) {
    const mj::Value* m = e->get("message");
    err = "api error: " + std::string((m && m->isStr()) ? m->str : "unknown");
    return false;
  }
  if (const mj::Value* mv = root.get("modelVersion")) if (mv->isStr()) modelVersion = mv->str;
  const mj::Value* cands = root.get("candidates");
  if (!cands || !cands->isArr() || cands->arr.empty()) {
    const mj::Value* pf = root.get("promptFeedback");
    const mj::Value* br = pf ? pf->get("blockReason") : nullptr;
    err = (br && br->isStr()) ? "blocked: " + br->str : "no candidates";
    return false;
  }
  const mj::Value* c0 = cands->at(0);
  std::string finish;
  if (const mj::Value* fr = c0->get("finishReason")) if (fr->isStr()) finish = fr->str;
  const mj::Value* content = c0->get("content");
  const mj::Value* parts = content ? content->get("parts") : nullptr;
  if (parts && parts->isArr()) {
    for (const auto& part : parts->arr) {
      const mj::Value* th = part.get("thought");
      if (th && th->isTrue()) continue;
      const mj::Value* tx = part.get("text");
      if (tx && tx->isStr()) text += tx->str;
    }
  }
  if (text.empty()) {
    err = "empty text (finishReason=" + (finish.empty() ? std::string("?") : finish) + ")";
    return false;
  }
  return true;
}

bool parseExplainJson(const std::string& textIn, ExplainText& out, std::string& err) {
  std::string s = trim(textIn);
  if (s.compare(0, 3, "```") == 0) {
    size_t nl = s.find('\n');
    s = (nl == std::string::npos) ? std::string() : s.substr(nl + 1);
    size_t end = s.rfind("```");
    if (end != std::string::npos) s = s.substr(0, end);
    s = trim(s);
  }
  mj::Value v;
  std::string perr;
  if (!mj::parse(s, v, perr)) { err = "output not JSON (" + perr + ")"; return false; }
  const mj::Value* h = v.get("headline");
  const mj::Value* b = v.get("body");
  if (!h || !h->isStr() || !b || !b->isStr()) { err = "missing headline/body"; return false; }
  out.headline = trim(h->str);
  out.body = trim(b->str);
  return true;
}

// ---------- validation ----------
bool validateExplain(const ExplainText& t, const ExplainContext& c, const std::string& contextJson, std::string& reason) {
  const std::string h = trim(t.headline), b = trim(t.body);
  if (h.empty() || b.empty()) { reason = "empty headline/body"; return false; }
  if (h.size() > 20) { reason = "headline too long (" + std::to_string(h.size()) + ")"; return false; }
  if (b.size() > 100) { reason = "body too long (" + std::to_string(b.size()) + ")"; return false; }
  const std::string all = h + " " + b;
  for (unsigned char ch : all) {
    if (ch < 0x20 || ch > 0x7E) { reason = "non-ASCII character"; return false; }
  }
  const std::set<std::string> ok = allowedNumbers(contextJson);
  for (const auto& n : numbersIn(all)) {
    if (!ok.count(n)) { reason = "number not in input: " + n; return false; }
  }
  for (const auto& w : wordsIn(all)) {
    for (const char* bw : kBannedExact)
      if (w == bw) { reason = "banned word: " + w; return false; }
    for (const char* bp : kBannedPrefix)
      if (w.compare(0, std::char_traits<char>::length(bp), bp) == 0) { reason = "banned word: " + w; return false; }
  }
  const std::string low = lower(all);
  if (c.state == "OK" || c.state == "WAITING") {
    for (const char* ph : kAskWater)
      if (low.find(ph) != std::string::npos) { reason = "asks to water in state " + c.state; return false; }
  }
  if (c.state == "DRY") {
    for (const char* ph : kNoNeed)
      if (low.find(ph) != std::string::npos) { reason = "says no water needed in DRY"; return false; }
  }
  reason.clear();
  return true;
}

// ---------- templates ----------
ExplainText templateExplain(const ExplainContext& c) {
  const std::string R = fmtRvs(c.R, c.L, c.H), L = fmtLH(c.L), H = fmtLH(c.H), W = std::to_string(c.waitMin);
  const std::string N = std::to_string(c.confirmRounds);
  const bool known = !std::isnan(c.R) && !std::isnan(c.L) && !std::isnan(c.H);
  auto waiting = [&]() {
    ExplainText t;
    t.headline = "Waiting";
    t.body = c.localTime.empty()
                 ? "Watering logged. Re-checking R after " + W + " min."
                 : "Watering logged at " + c.localTime + ". Re-checking R after " + W + " min.";
    return t;
  };
  // fw1.4: 每一句都按 R 与 L、H 的实际大小选择, 不假设"缺水就一定低于 H"或"不缺水就一定高于 L"
  auto dry = [&]() {
    ExplainText t;
    t.headline = "Check soil";
    if (known && c.R < c.L) {
      t.headline = "Soil drying";
      t.body = "R " + R + " is below this pot's check line " + L + ". Please check the soil.";
    } else if (known && c.R >= c.H) {
      t.body = "R " + R + " is at or above " + H + ", but not yet for " + N + " readings. Please check the soil.";
    } else if (known) {
      t.body = "R " + R + " is still below " + H + ". Please check the soil.";
    } else {
      t.body = "Recovery is not confirmed yet. Please check the soil.";
    }
    return t;
  };
  auto ok = [&]() {
    ExplainText t;
    t.headline = "In range";
    if (known && c.R >= c.H) {
      t.body = "R " + R + " is at or above " + H + ". No water needed now.";
    } else if (known && c.R >= c.L) {
      t.body = "R " + R + " is at or above the check line " + L + ". No water needed now.";
    } else if (known) {
      t.headline = "Watching";
      t.body = "R " + R + " is below " + L + ", but not yet for " + N + " readings in a row.";
    } else {
      t.body = "No change in state.";
    }
    return t;
  };
  switch (c.event) {
    case ExplainEvent::DryToWait: return waiting();
    case ExplainEvent::WaitToOk:
      return {"Recovered", "R reached " + R + " (>= " + H + ") after watering. No water needed now."};
    case ExplainEvent::WaitToDry:   // fw1.4: 最后一轮可能已达 H, 只是最近连续 N 轮没有全部达到
      return {"Not confirmed", "R is " + R + " now, but the last " + N + " readings were not all >= " + H + ". Please check again."};
    case ExplainEvent::DryToOk:
      return {"Recovered", "R reached " + R + " (>= " + H + "). No water needed now."};
    case ExplainEvent::OkToDry: return dry();
    case ExplainEvent::FirstState:
    case ExplainEvent::Manual:
    default:
      if (c.state == "DRY") return dry();
      if (c.state == "WAITING") return waiting();
      return ok();
  }
}

std::vector<std::string> wrapWords(const std::string& text, size_t width) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') i++;
    size_t j = i;
    while (j < text.size() && text[j] != ' ') j++;
    std::string word = text.substr(i, j - i);
    i = j;
    if (word.empty()) break;
    while (word.size() > width) {  // 超长单词硬换行
      if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
      lines.push_back(word.substr(0, width));
      word = word.substr(width);
    }
    if (cur.empty()) cur = word;
    else if (cur.size() + 1 + word.size() <= width) cur += " " + word;
    else { lines.push_back(cur); cur = word; }
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}
