#pragma once
// SD kartes aizstājējs datora būvēm. Faili atmiņā (g_sdFiles); virtuālā roka tos
// periodiski ielasa no mapes (g_sdDir) uz datora.
#include <map>
#include <string>
#include <io.h>

static std::map<std::string, std::string> g_sdFiles;   // NOSAUKUMS.TXT -> saturs
static bool g_sdPresent = true;
static std::string g_sdDir;                              // tukšs = tikai atmiņa

static std::string sdUpper(std::string s) {
  for (char &c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
  return s;
}

static void sdReloadDir() {
  if (g_sdDir.empty()) return;
  g_sdFiles.clear();
  _finddata_t fd;
  intptr_t h = _findfirst((g_sdDir + "\\*.txt").c_str(), &fd);
  if (h == -1) return;
  do {
    FILE *f = fopen((g_sdDir + "\\" + fd.name).c_str(), "rb");
    if (!f) continue;
    std::string data;
    char b[512];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) data.append(b, n);
    fclose(f);
    g_sdFiles[sdUpper(fd.name)] = data;
  } while (_findnext(h, &fd) == 0);
  _findclose(h);
}

namespace sdcard {
namespace {
std::string cur;
size_t pos = 0;
bool isOpen = false;
uint32_t lastReloadMs = 0;
bool on = false;
}
void setEnabled(bool en) { on = en; if (en) sdReloadDir(); }
bool enabled() { return on; }
void poll() {
  if (!isOpen && millis() - lastReloadMs > 1000) { lastReloadMs = millis(); sdReloadDir(); }
}
bool available() { return on && g_sdPresent; }
uint8_t count() { return available() ? (uint8_t)g_sdFiles.size() : 0; }
bool nameAt(uint8_t i, char *out) {
  if (!available() || i >= g_sdFiles.size()) return false;
  auto it = g_sdFiles.begin();
  std::advance(it, i);
  strncpy(out, it->first.c_str(), 12);
  out[12] = 0;
  return true;
}
bool open(const char *name) {
  auto it = g_sdFiles.find(sdUpper(name));
  if (!available() || it == g_sdFiles.end()) return false;
  cur = it->second;
  pos = 0;
  isOpen = true;
  return true;
}
int readLine(char *buf, uint8_t size) {
  if (!isOpen || pos >= cur.size()) return -1;
  uint8_t n = 0;
  while (pos < cur.size()) {
    char c = cur[pos++];
    if (c == '\n') break;
    if (c == '\r') continue;
    if (n < size - 1) buf[n++] = c;
  }
  buf[n] = 0;
  return n;
}
void rewind() { pos = 0; }
void close() { isOpen = false; }
}  // namespace sdcard
