#include "AiClient.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "config.h"

#define MTX (static_cast<SemaphoreHandle_t>(mutex_))

void AiClient::begin(const char* rootCa, bool startTask) {
  rootCa_ = rootCa;
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (startTask && !task_) {
    TaskHandle_t h = nullptr;
    // 运行在 core 0 (Wi-Fi 所在核), 低优先级; TLS 需要较大栈
    xTaskCreatePinnedToCore(&AiClient::taskThunk, "ai_explain", 16384, this, 1, &h, 0);
    task_ = h;
  }
}

void AiClient::configure(const String& apiKey, const String& model, bool enabled) {
  xSemaphoreTake(MTX, portMAX_DELAY);
  apiKey_ = apiKey.c_str();
  model_ = model.length() ? model.c_str() : AI_DEFAULT_MODEL;
  enabled_ = enabled;
  xSemaphoreGive(MTX);
}

bool AiClient::hasKey() const { return !apiKey_.empty(); }

String AiClient::model() const { return String(model_.c_str()); }

void AiClient::submit(const ExplainContext& ctx, bool manual, uint32_t delayMs) {
  xSemaphoreTake(MTX, portMAX_DELAY);
  pendingCtx_ = ctx;
  pendingManual_ = manual;
  pendingDue_ = millis() + delayMs;
  pendingId_ = nextId_++;
  pendingValid_ = true;
  xSemaphoreGive(MTX);
}

bool AiClient::poll(AiResult& out) {
  if (!resultReady_) return false;
  xSemaphoreTake(MTX, portMAX_DELAY);
  out = result_;
  resultReady_ = false;
  xSemaphoreGive(MTX);
  return true;
}

void AiClient::taskThunk(void* self) { static_cast<AiClient*>(self)->taskLoop(); }

bool AiClient::wouldCallNetwork(const std::string& key, std::string& why) const {
  if (!enabled_) { why = "ai_off"; return false; }
  if (key.empty()) { why = "no_api_key"; return false; }
  if (!networkOk_) { why = "offline"; return false; }
  if (callsToday_ >= AI_DAILY_MAX) { why = "daily_cap"; return false; }
  return true;
}

void AiClient::taskLoop() {
  for (;;) {
    bool have = false;
    ExplainContext ctx;
    bool manual = false;
    uint32_t id = 0;
    xSemaphoreTake(MTX, portMAX_DELAY);
    const uint32_t now = millis();
    if (pendingValid_ && static_cast<int32_t>(now - pendingDue_) >= 0) {
      std::string why;
      const bool net = wouldCallNetwork(apiKey_, why);
      const uint32_t gap = pendingManual_ ? AI_MANUAL_MIN_INTERVAL_MS : AI_MIN_INTERVAL_MS;
      // 只有真的要联网时才受最短间隔限制; 回退模板不用等
      if (!net || lastCallMs_ == 0 || now - lastCallMs_ >= gap) {
        ctx = pendingCtx_;
        manual = pendingManual_;
        id = pendingId_;
        pendingValid_ = false;
        working_ = true;
        have = true;
      }
    }
    xSemaphoreGive(MTX);
    if (!have) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    AiResult r = process(ctx, manual, id);
    xSemaphoreTake(MTX, portMAX_DELAY);
    result_ = r;
    resultReady_ = true;
    working_ = false;
    xSemaphoreGive(MTX);
  }
}

AiResult AiClient::runNow(const ExplainContext& ctx, bool manual) { return process(ctx, manual, nextId_++); }

bool AiClient::httpPost(const std::string& url, const std::string& key, const std::string& body, std::string& resp,
                        int& code, uint32_t& ms, std::string& err) {
  WiFiClientSecure client;
  if (insecure_) client.setInsecure();
  else client.setCACert(rootCa_);
  client.setHandshakeTimeout((AI_TIMEOUT_MS + 999) / 1000);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(AI_TIMEOUT_MS);
  http.setTimeout(AI_TIMEOUT_MS);
  const uint32_t t0 = millis();
  if (!http.begin(client, String(url.c_str()))) {
    err = "http.begin failed";
    ms = millis() - t0;
    code = 0;
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-goog-api-key", String(key.c_str()));
  code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  if (code > 0) {
    resp = http.getString().c_str();
  } else {
    err = HTTPClient::errorToString(code).c_str();
  }
  http.end();
  ms = millis() - t0;
  return code > 0;
}

AiResult AiClient::process(const ExplainContext& ctx, bool manual, uint32_t id) {
  AiResult r;
  r.id = id;
  r.event = ctx.event;
  r.state = ctx.state;
  r.manual = manual;
  r.contextJson = buildContextJson(ctx);
  std::string key, model;
  xSemaphoreTake(MTX, portMAX_DELAY);
  key = apiKey_;
  model = model_.empty() ? std::string(AI_DEFAULT_MODEL) : model_;
  xSemaphoreGive(MTX);
  r.model = model;

  auto fallback = [&](const std::string& why) {
    r.text = templateExplain(ctx);
    r.fromAi = false;
    r.reason = why;
    if (verbose_) Serial.printf("[ai #%lu] TPL (%s): %s | %s\n", static_cast<unsigned long>(id), why.c_str(),
                                r.text.headline.c_str(), r.text.body.c_str());
    return r;
  };

  std::string why;
  if (!wouldCallNetwork(key, why)) return fallback(why);
  if (ESP.getFreeHeap() < AI_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < AI_MIN_BLOCK_HEAP) return fallback("low_heap");

  char url[200];
  snprintf(url, sizeof(url), AI_HOST_PATH_FMT, model.c_str());
  if (verbose_) {
    Serial.printf("[ai #%lu] request event=%s model=%s\n[ai #%lu] input %s\n", static_cast<unsigned long>(id),
                  explainEventName(ctx.event), model.c_str(), static_cast<unsigned long>(id), r.contextJson.c_str());
  }
  std::string hint;
  for (uint8_t attempt = 1; attempt <= 2; attempt++) {
    const std::string body = buildRequestBody(r.contextJson, hint);
    std::string resp, err;
    int code = 0;
    uint32_t ms = 0;
    const bool sent = httpPost(url, key, body, resp, code, ms, err);
    callsToday_++;
    lastCallMs_ = millis();
    r.attempts = attempt;
    r.httpCode = code;
    r.latencyMs = ms;
    if (verbose_) {
      Serial.printf("[ai #%lu] attempt %u: http %d, %lu ms\n", static_cast<unsigned long>(id), attempt, code,
                    static_cast<unsigned long>(ms));
      if (!resp.empty()) Serial.printf("[ai #%lu] raw %s\n", static_cast<unsigned long>(id), resp.substr(0, 2000).c_str());
    }
    if (!sent) return fallback("http_error: " + err);
    if (code != 200) {
      const std::string m = apiErrorMessage(resp);
      return fallback("http_" + std::to_string(code) + (m.empty() ? "" : ": " + m.substr(0, 120)));
    }
    std::string text, mv;
    if (!extractModelText(resp, text, mv, err)) {
      if (attempt == 2) return fallback(err);
      hint = "Your previous answer was unusable (" + err + "). Return the JSON object only.";
      continue;
    }
    r.modelVersion = mv;
    ExplainText t;
    if (!parseExplainJson(text, t, err)) {
      if (attempt == 2) return fallback(err);
      hint = "Your previous answer was unusable (" + err + "). Return the JSON object only.";
      continue;
    }
    if (!validateExplain(t, ctx, r.contextJson, why)) {
      if (verbose_) Serial.printf("[ai #%lu] rejected: %s\n", static_cast<unsigned long>(id), why.c_str());
      if (attempt == 2) return fallback("invalid: " + why);
      hint = "Your previous answer was rejected by the device check: " + why + ". Follow every rule.";
      continue;
    }
    r.text = t;
    r.fromAi = true;
    r.reason = "ok";
    if (verbose_) Serial.printf("[ai #%lu] AI: %s | %s\n", static_cast<unsigned long>(id), t.headline.c_str(), t.body.c_str());
    return r;
  }
  return fallback("unreachable");
}
