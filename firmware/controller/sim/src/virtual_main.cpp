// =============================================================================
//  Virtuālā roka: ĪSTAIS kontroliera firmware, kompilēts Windows datoram.
//  USB seriālā porta vietā - TCP ports (noklusējums 5555) vai COM ports:
//    program.exe          -> socket://localhost:5555
//    program.exe COM11    -> virtuālā COM pāra viena puse (com0com), PC programma / Printrun / jebkurš
//                            terminālis atver otru pusi (piem. COM10) kā parastu plati.
//
//  Simulē: soļu ISR reālajā laikā, gala slēdžus (NO, abās pusēs pie ±VIRT_SWITCH_DEG
//  no starta pozas), iestatījumu atmiņu (fails virtual_eeprom.bin).
//    pio run -e virtual  &&  .pio\build\virtual\program.exe [5555 | COMx]
// =============================================================================
#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <direct.h>
#include "Arduino.h"
#include "../../src/stepper.cpp"
#include "../../src/motion.cpp"
#include "../../src/endstops.cpp"
#include "../../src/gripper.cpp"
#include "../../src/tmc2209.cpp"
#include "../../src/storage.cpp"
#include "../../src/protocol.cpp"
#include "../../src/lcd.cpp"
#include "../../src/runner.cpp"
#include "../../src/ui.cpp"
#include "../../src/main.cpp"
#include "mock_hal.h"
#include "mock_sd.h"

MockSerial Serial, Serial1, Serial2;

static const float kSwitchDeg[NUM_JOINTS] = { 100, 80, 120, 110, 160 };   // fiziskais attālums no starta
static const pin_id_t kEsPins[NUM_JOINTS] = ENDSTOP_PINS;
static const char *kStoreFile = "virtual_eeprom.bin";

static void loadStorageFile() {
  FILE *f = fopen(kStoreFile, "rb");
  if (!f) return;
  fread(g_storage.data(), 1, g_storage.size(), f);
  fclose(f);
}

static void saveStorageFile() {
  FILE *f = fopen(kStoreFile, "wb");
  if (!f) return;
  fwrite(g_storage.data(), 1, g_storage.size(), f);
  fclose(f);
}

// ---------------------------------------------------------------------------
//  Savienojums ar "datoru": TCP ports vai COM ports (virtuālais pāris, piem. com0com)
// ---------------------------------------------------------------------------
struct Transport {
  virtual ~Transport() {}
  // Nolasa ienākošos baitus. Atgriež true, ja tikko pieslēdzās jauns klients.
  virtual bool poll(std::string &in) = 0;
  virtual bool connected() = 0;
  virtual int write(const char *d, int n) = 0;
};

struct TcpTransport : Transport {
  SOCKET srv = INVALID_SOCKET, cli = INVALID_SOCKET;
  bool open(int port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)port);
    if (bind(srv, (sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 1) != 0) return false;
    u_long nb = 1;
    ioctlsocket(srv, FIONBIO, &nb);
    return true;
  }
  bool poll(std::string &in) override {
    bool fresh = false;
    if (cli == INVALID_SOCKET) {
      cli = accept(srv, nullptr, nullptr);
      if (cli == INVALID_SOCKET) return false;
      u_long nb = 1;
      ioctlsocket(cli, FIONBIO, &nb);
      BOOL nd = TRUE;
      setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof nd);
      fresh = true;
    }
    char buf[1024];
    const int n = recv(cli, buf, sizeof buf, 0);
    if (n > 0) in.append(buf, n);
    else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
      closesocket(cli);
      cli = INVALID_SOCKET;
      printf("Klients atvienojās\n");
    }
    return fresh;
  }
  bool connected() override { return cli != INVALID_SOCKET; }
  int write(const char *d, int n) override { return send(cli, d, n, 0); }
};

// COM ports. Kad otrā pusē kāds atver portu, tā DTR mums parādās kā DSR (com0com
// noklusējums) -> uzskatām to par "porta atvēršanu" un izdrukājam [READY] kā Mega pēc reset.
struct ComTransport : Transport {
  HANDLE h = INVALID_HANDLE_VALUE;
  bool dsrWorks = false, peer = false;
  uint32_t openedMs = 0;
  bool open(const char *name) {
    std::string path = std::string("\\\\.\\") + name;
    h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DCB dcb = {};
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = dcb.fInX = FALSE;
    SetCommState(h, &dcb);
    COMMTIMEOUTS t = {MAXDWORD, 0, 0, 0, 50};   // lasīšana atgriežas uzreiz
    SetCommTimeouts(h, &t);
    openedMs = GetTickCount();
    return true;
  }
  bool poll(std::string &in) override {
    bool fresh = false;
    DWORD ms = 0;
    if (GetCommModemStatus(h, &ms)) {
      const bool dsr = ms & MS_DSR_ON;
      if (dsr) dsrWorks = true;
      if (dsr && !peer) { peer = true; fresh = true; }
      if (!dsr && peer && dsrWorks) { peer = false; printf("Klients atvienojās\n"); }
    }
    // Ja DSR nekad neparādās (cits virtuālā porta draiveris), pēc 3 s uzskatām, ka klients ir.
    if (!dsrWorks && !peer && GetTickCount() - openedMs > 3000) { peer = true; fresh = true; }
    char buf[1024];
    DWORD n = 0;
    if (ReadFile(h, buf, sizeof buf, &n, nullptr) && n) in.append(buf, n);
    return fresh;
  }
  bool connected() override { return peer; }
  int write(const char *d, int n) override {
    DWORD w = 0;
    return WriteFile(h, d, (DWORD)n, &w, nullptr) ? (int)w : -1;
  }
};

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char *arg = argc > 1 ? argv[1] : "5555";
  timeBeginPeriod(1);

  Transport *tr = nullptr;
  char where[64];
  if (_strnicmp(arg, "COM", 3) == 0) {
    ComTransport *c = new ComTransport();
    if (!c->open(arg)) {
      printf("Nevar atvērt %s (nav tāda porta vai to jau izmanto cita programma).\n", arg);
      printf("Virtuālam COM portam vajag pāri, piem. com0com: COM10 <-> COM11.\n");
      return 1;
    }
    tr = c;
    snprintf(where, sizeof where, "%s", arg);
  } else {
    TcpTransport *t = new TcpTransport();
    if (!t->open(atoi(arg))) {
      printf("Nevar atvērt TCP portu %s (jau aizņemts?)\n", arg);
      return 1;
    }
    tr = t;
    snprintf(where, sizeof where, "127.0.0.1:%s", arg);
  }

  loadStorageFile();

  // Virtuālā SD karte = mape "sdcard" darba mapē (tur var iekopēt *.TXT no PC programmas)
  _mkdir("sdcard");
  g_sdDir = "sdcard";
  sdReloadDir();
  if (g_sdFiles.empty()) {
    FILE *f = fopen("sdcard\\DEMO.TXT", "wb");
    if (f) {
      fputs("; Demo programma virtualajai rokai (vispirms ZERO vai HOME)\r\n"
            "MOVE 30 20 -15 0 0 S60\r\nGRIP 100\r\nWAIT 500\r\n"
            "MOVE -30 10 15 20 90 S60\r\nGRIP 0\r\nWAIT 300\r\nMOVE 0 0 0 0 0 S80\r\n", f);
      fclose(f);
    }
  }
  const auto t0 = std::chrono::steady_clock::now();
  auto nowUs = [&]() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  };

  setup();
  Serial.out.clear();
  printf("Virtuala roka (" BOARD_NAME " firmware " FIRMWARE_VERSION ") klausas: %s\n", where);
  printf("Gala sledzi: J1..J5 pie +-%.0f/%.0f/%.0f/%.0f/%.0f grad. no starta pozas. Ctrl+C - beigt.\n",
         kSwitchDeg[0], kSwitchDeg[1], kSwitchDeg[2], kSwitchDeg[3], kSwitchDeg[4]);

  int64_t phys[NUM_JOINTS] = {0};
  int32_t prevPos[NUM_JOINTS] = {0};
  uint64_t isrUs = 0;
  const double isrPeriod = 1e6 / STEP_ISR_HZ;
  double isrPhase = 0;
  std::vector<uint8_t> lastSaved = g_storage;

  for (;;) {
    // --- savienojums
    std::string rx;
    if (tr->poll(rx)) {
      Serial.in.clear();
      Serial.out.clear();
      printf("Klients pieslēdzās\n");
      protocol::banner();   // kā īsta plate pēc porta atvēršanas
    }
    if (tr->connected()) Serial.in += rx;

    // --- soļu ISR reālajā laikā
    const uint64_t now = nowUs();
    double ticks = (double)(now - isrUs) / isrPeriod + isrPhase;
    if (ticks > 20000) ticks = 20000;   // ja dators aizķērās
    const int nt = (int)ticks;
    isrPhase = ticks - nt;
    for (int i = 0; i < nt; i++) {
      g_simUs = isrUs + (uint64_t)(i * isrPeriod);
      stepper_isr();
    }
    isrUs = now;
    g_simUs = now;

    for (int j = 0; j < NUM_JOINTS; j++) {
      const int32_t p = stepper::position(j);
      phys[j] += p - prevPos[j];
      const double deg = (double)phys[j] / motion::joint(j).stepsPerDeg;
      setPinLevel(kEsPins[j], !(deg <= -kSwitchDeg[j] || deg >= kSwitchDeg[j]));   // NO uz GND
    }

    loop();   // īstā firmware cilpa
    for (int j = 0; j < NUM_JOINTS; j++) prevPos[j] = stepper::position(j);

    if (g_storage != lastSaved) { saveStorageFile(); lastSaved = g_storage; }

    // --- izvade (bez klienta - izmetam, kā īsta plate bez atvērta porta)
    if (!Serial.out.empty()) {
      if (tr->connected()) {
        const int s = tr->write(Serial.out.data(), (int)Serial.out.size());
        if (s > 0) Serial.out.erase(0, s);
      } else {
        Serial.out.clear();
      }
    }
    Serial1.out.clear();
    Sleep(1);
  }
}