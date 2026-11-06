#include "MiniJson.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace mj {

std::string escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  return o;
}

const Value* Value::get(const char* key) const {
  if (kind != T_OBJ) return nullptr;
  for (const auto& kv : obj)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

const Value* Value::at(size_t i) const {
  if (kind != T_ARR || i >= arr.size()) return nullptr;
  return &arr[i];
}

namespace {

struct Parser {
  const std::string& t;
  size_t p = 0;
  std::string err;
  explicit Parser(const std::string& text) : t(text) {}

  bool fail(const char* msg) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s at %u", msg, static_cast<unsigned>(p));
    err = buf;
    return false;
  }
  void ws() {
    while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) p++;
  }
  static void utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) {
      o += static_cast<char>(cp);
    } else if (cp < 0x800) {
      o += static_cast<char>(0xC0 | (cp >> 6));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      o += static_cast<char>(0xE0 | (cp >> 12));
      o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      o += static_cast<char>(0xF0 | (cp >> 18));
      o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      o += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(unsigned& v) {
    if (p + 4 > t.size()) return fail("short \\u");
    v = 0;
    for (int i = 0; i < 4; i++) {
      char c = t[p++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (c - '0');
      else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
      else return fail("bad \\u");
    }
    return true;
  }
  bool str(std::string& o) {
    if (t[p] != '"') return fail("expected string");
    p++;
    while (p < t.size()) {
      char c = t[p++];
      if (c == '"') return true;
      if (c != '\\') { o += c; continue; }
      if (p >= t.size()) return fail("bad escape");
      char e = t[p++];
      switch (e) {
        case '"': o += '"'; break;
        case '\\': o += '\\'; break;
        case '/': o += '/'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'n': o += '\n'; break;
        case 'r': o += '\r'; break;
        case 't': o += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
            p += 2;
            unsigned lo;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(o, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool value(Value& v, int depth) {
    if (depth > 40) return fail("too deep");
    ws();
    if (p >= t.size()) return fail("unexpected end");
    char c = t[p];
    if (c == '{') {
      v.kind = Value::T_OBJ;
      p++;
      ws();
      if (p < t.size() && t[p] == '}') { p++; return true; }
      while (true) {
        ws();
        std::string key;
        if (p >= t.size() || !str(key)) return err.empty() ? fail("expected key") : false;
        ws();
        if (p >= t.size() || t[p] != ':') return fail("expected ':'");
        p++;
        Value child;
        if (!value(child, depth + 1)) return false;
        v.obj.emplace_back(std::move(key), std::move(child));
        ws();
        if (p < t.size() && t[p] == ',') { p++; continue; }
        if (p < t.size() && t[p] == '}') { p++; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      v.kind = Value::T_ARR;
      p++;
      ws();
      if (p < t.size() && t[p] == ']') { p++; return true; }
      while (true) {
        Value child;
        if (!value(child, depth + 1)) return false;
        v.arr.push_back(std::move(child));
        ws();
        if (p < t.size() && t[p] == ',') { p++; continue; }
        if (p < t.size() && t[p] == ']') { p++; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') { v.kind = Value::T_STR; return str(v.str); }
    if (t.compare(p, 4, "true") == 0) { v.kind = Value::T_BOOL; v.b = true; p += 4; return true; }
    if (t.compare(p, 5, "false") == 0) { v.kind = Value::T_BOOL; v.b = false; p += 5; return true; }
    if (t.compare(p, 4, "null") == 0) { v.kind = Value::T_NULL; p += 4; return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t s = p;
      p++;
      while (p < t.size() && (std::strchr("0123456789.eE+-", t[p]) != nullptr)) p++;
      v.kind = Value::T_NUM;
      v.str = t.substr(s, p - s);
      v.num = std::strtod(v.str.c_str(), nullptr);
      return true;
    }
    return fail("unexpected character");
  }
};

}  // namespace

bool parse(const std::string& text, Value& out, std::string& err) {
  Parser ps(text);
  out = Value();
  if (!ps.value(out, 0)) { err = ps.err; return false; }
  ps.ws();
  if (ps.p != text.size()) { err = "trailing characters"; return false; }
  return true;
}

}  // namespace mj
