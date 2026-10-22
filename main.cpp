// ============================================================
// Seven Tools of the Bandit - PC Edition (Windows .exe)
// Emulador/puerto para escritorio del firmware original de
// M5Stack Cardputer "Seven Tools of the Bandit".
// Pantalla 240x135 fiel al original, menus y sonidos iguales.
// Las herramientas de red son REALES: usan la tarjeta de red
// de tu PC (WiFi scan vía WLAN API, ping/portscan/HTTP/DNS vía
// Winsock, traceroute vía ICMP, etc).
//
// Lo que NO tiene equivalente real en una PC se marca con un
// aviso explicito en pantalla en vez de simular datos falsos:
//   - Modo promiscuo de paquetes WiFi (exclusivo del chip ESP32)
//   - Que la PC se anuncie a si misma como teclado/mouse USB
//   - Modo Access Point propio del servidor de archivos
//
// Compilar (MSYS2 MinGW64 o GitHub Actions, ver README.md):
//   g++ -O2 -std=c++17 src/main.cpp -o SevenToolsBandit.exe ^
//       -lmingw32 -lSDL2main -lSDL2 -lws2_32 -liphlpapi ^
//       -lwlanapi -licmp -lole32 -static -static-libgcc -static-libstdc++
// ============================================================

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define UNICODE
#define _UNICODE

#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <iphlpapi.h>
  #include <icmpapi.h>
  #include <wlanapi.h>
  #include <shlobj.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "wlanapi.lib")
  #pragma comment(lib, "ole32.lib")
#endif

// ============================================================
// Constantes de pantalla (identicas al Cardputer: 240x135)
// ============================================================
constexpr int W = 240, H = 135;
constexpr int SCALE = 5;              // factor de ventana en PC
constexpr int WIN_W = W * SCALE, WIN_H = H * SCALE;

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

// ---------- Paleta "Bandit": tonos tierra, oro viejo y verde terminal ----------
const uint32_t BG0      = rgb(20, 17, 24);
const uint32_t BG1      = rgb(30, 25, 34);
const uint32_t BG2      = rgb(42, 35, 46);
const uint32_t INK      = rgb(235, 222, 200);
const uint32_t INK_DIM  = rgb(150, 136, 128);
const uint32_t BANDIT_GOLD = rgb(214, 163, 80);
const uint32_t GOLD_LT   = rgb(245, 205, 130);
const uint32_t RUST      = rgb(176, 82, 56);
const uint32_t SAGE      = rgb(122, 162, 110);
const uint32_t DUSK      = rgb(92, 102, 140);
const uint32_t PLUM      = rgb(132, 86, 122);
const uint32_t METAL     = rgb(120, 112, 108);
const uint32_t METAL_D   = rgb(58, 52, 54);
const uint32_t C_BLACK   = rgb(0, 0, 0);
const uint32_t C_WHITE   = rgb(255, 250, 240);

float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }


// ============================================================
// Canvas: envoltorio sobre SDL2 que imita la API de M5Canvas
// usada por el firmware original (drawString, fillRect, etc).
// Todo se dibuja en un render-target de 240x135 y se escala
// al tamano de ventana real al final de cada frame.
// ============================================================
enum TextDatum { top_left, top_center, top_right, middle_left,
                 middle_center, middle_right, bottom_left,
                 bottom_center, bottom_right };

struct Canvas {
  SDL_Renderer *ren = nullptr;
  SDL_Texture  *target = nullptr;
  TTF_Font     *font1 = nullptr;   // textSize 1  (~6x8 px aprox)
  TTF_Font     *font2 = nullptr;   // textSize 2
  TTF_Font     *fontBold1 = nullptr;
  uint32_t textColor = C_WHITE;
  int textDatum = top_left;
  int textSize = 1;
  bool boldText = false;

  void init(SDL_Renderer *r) {
    ren = r;
    target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_TARGET, W, H);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    font1 = TTF_OpenFont("assets/font.ttf", 8);
    font2 = TTF_OpenFont("assets/font.ttf", 15);
    fontBold1 = TTF_OpenFont("assets/font_bold.ttf", 8);
    if (!font1 || !font2) {
      // fallback: buscar junto al ejecutable o en Windows Fonts
      font1 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 8);
      font2 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 15);
    }
  }
  void begin() { SDL_SetRenderTarget(ren, target); }
  static void setColor(SDL_Renderer *ren, uint32_t c) {
    SDL_SetRenderDrawColor(ren, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
  }
  void fillScreen(uint32_t c) { setColor(ren, c); SDL_RenderClear(ren); }
  void fillRect(int x, int y, int w, int h, uint32_t c) {
    setColor(ren, c); SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(ren, &r);
  }
  void drawRect(int x, int y, int w, int h, uint32_t c) {
    setColor(ren, c); SDL_Rect r{x, y, w, h}; SDL_RenderDrawRect(ren, &r);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint32_t c) {
    setColor(ren, c); SDL_RenderDrawLine(ren, x0, y0, x1, y1);
  }
  void drawFastHLine(int x, int y, int w, uint32_t c) { drawLine(x, y, x + w - 1, y, c); }
  void drawFastVLine(int x, int y, int h, uint32_t c) { drawLine(x, y, x, y + h - 1, c); }
  void drawPixel(int x, int y, uint32_t c) { setColor(ren, c); SDL_RenderDrawPoint(ren, x, y); }
  void drawCircleImpl(int cx, int cy, int radius, uint32_t c, bool fill) {
    setColor(ren, c);
    int x = radius, y = 0, err = 0;
    while (x >= y) {
      if (fill) {
        SDL_RenderDrawLine(ren, cx - x, cy + y, cx + x, cy + y);
        SDL_RenderDrawLine(ren, cx - x, cy - y, cx + x, cy - y);
        SDL_RenderDrawLine(ren, cx - y, cy + x, cx + y, cy + x);
        SDL_RenderDrawLine(ren, cx - y, cy - x, cx + y, cy - x);
      } else {
        SDL_RenderDrawPoint(ren, cx + x, cy + y); SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx + x, cy - y); SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x); SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x); SDL_RenderDrawPoint(ren, cx - y, cy - x);
      }
      if (err <= 0) { y += 1; err += 2 * y + 1; }
      if (err > 0) { x -= 1; err -= 2 * x + 1; }
    }
  }
  void drawCircle(int cx, int cy, int r, uint32_t c) { drawCircleImpl(cx, cy, r, c, false); }
  void fillCircle(int cx, int cy, int r, uint32_t c) { drawCircleImpl(cx, cy, r, c, true); }
  void drawEllipse(int cx, int cy, int rx, int ry, uint32_t c) {
    setColor(ren, c);
    for (float t = 0; t < 6.2832f; t += 0.08f)
      SDL_RenderDrawPoint(ren, cx + (int)(rx * cosf(t)), cy + (int)(ry * sinf(t)));
  }
  void fillEllipse(int cx, int cy, int rx, int ry, uint32_t c) {
    setColor(ren, c);
    for (int y = -ry; y <= ry; y++) {
      int dx = (int)(rx * sqrtf(1.0f - (float)(y * y) / (float)(ry * ry)));
      SDL_RenderDrawLine(ren, cx - dx, cy + y, cx + dx, cy + y);
    }
  }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
    drawLine(x0, y0, x1, y1, c); drawLine(x1, y1, x2, y2, c); drawLine(x2, y2, x0, y0, c);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
    setColor(ren, c);
    int minY = std::min({y0, y1, y2}), maxY = std::max({y0, y1, y2});
    for (int y = minY; y <= maxY; y++) {
      std::vector<int> xs;
      auto edge = [&](int ax, int ay, int bx, int by) {
        if (ay == by) return;
        if (y < std::min(ay, by) || y > std::max(ay, by)) return;
        float t = (float)(y - ay) / (float)(by - ay);
        xs.push_back(ax + (int)(t * (bx - ax)));
      };
      edge(x0, y0, x1, y1); edge(x1, y1, x2, y2); edge(x2, y2, x0, y0);
      if (xs.size() >= 2) {
        std::sort(xs.begin(), xs.end());
        SDL_RenderDrawLine(ren, xs.front(), y, xs.back(), y);
      }
    }
  }
  void roundRectImpl(int x, int y, int w, int h, int r, uint32_t c, bool fill) {
    if (r <= 0) { fill ? fillRect(x, y, w, h, c) : drawRect(x, y, w, h, c); return; }
    if (fill) {
      fillRect(x + r, y, w - 2 * r, h, c);
      fillRect(x, y + r, w, h - 2 * r, c);
      fillCircle(x + r, y + r, r, c); fillCircle(x + w - r - 1, y + r, r, c);
      fillCircle(x + r, y + h - r - 1, r, c); fillCircle(x + w - r - 1, y + h - r - 1, r, c);
    } else {
      drawFastHLine(x + r, y, w - 2 * r, c); drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
      drawFastVLine(x, y + r, h - 2 * r, c); drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
      drawCircle(x + r, y + r, r, c); drawCircle(x + w - r - 1, y + r, r, c);
      drawCircle(x + r, y + h - r - 1, r, c); drawCircle(x + w - r - 1, y + h - r - 1, r, c);
    }
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint32_t c) { roundRectImpl(x, y, w, h, r, c, true); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint32_t c) { roundRectImpl(x, y, w, h, r, c, false); }

  void setTextColor(uint32_t c) { textColor = c; }
  void setTextDatum(int d) { textDatum = d; }
  void setTextSize(int s) { textSize = s; }
  void setBold(bool b) { boldText = b; }

  TTF_Font *activeFont() {
    if (textSize >= 2) return font2;
    return boldText ? fontBold1 : font1;
  }
  int textWidth(const std::string &s) {
    if (s.empty()) return 0;
    int w2 = 0, h2 = 0;
    TTF_Font *f = activeFont();
    if (f) TTF_SizeUTF8(f, s.c_str(), &w2, &h2);
    return w2;
  }
  void drawString(const std::string &s, int x, int y) {
    if (s.empty()) return;
    TTF_Font *f = activeFont();
    if (!f) return;
    SDL_Color col{(Uint8)((textColor >> 16) & 0xFF), (Uint8)((textColor >> 8) & 0xFF),
                  (Uint8)(textColor & 0xFF), (Uint8)((textColor >> 24) & 0xFF)};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s.c_str(), col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);
    int dx = x, dy = y;
    switch (textDatum) {
      case top_left:      dx = x;          dy = y;          break;
      case top_center:    dx = x - tw / 2; dy = y;          break;
      case top_right:     dx = x - tw;     dy = y;          break;
      case middle_left:   dx = x;          dy = y - th / 2; break;
      case middle_center: dx = x - tw / 2; dy = y - th / 2; break;
      case middle_right:  dx = x - tw;     dy = y - th / 2; break;
      case bottom_left:   dx = x;          dy = y - th;     break;
      case bottom_center: dx = x - tw / 2; dy = y - th;     break;
      case bottom_right:  dx = x - tw;     dy = y - th;     break;
    }
    SDL_Rect dst{dx, dy, tw, th};
    SDL_RenderCopy(ren, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
  }
  void present(SDL_Renderer *screenRen) {
    SDL_SetRenderTarget(ren, nullptr);
    SDL_Rect dst{0, 0, WIN_W, WIN_H};
    SDL_RenderCopy(ren, target, nullptr, &dst);
  }
};

Canvas canvas;

// ============================================================
// Sonido: beep() simple con onda cuadrada generada en vivo,
// igual de uso que M5Cardputer.Speaker.tone(freq, ms)
// ============================================================
SDL_AudioDeviceID audioDev = 0;
std::atomic<bool> muted{false};
struct BeepState { double freq = 0; int samplesLeft = 0; double phase = 0; std::mutex mtx; } beepState;

void audioCallback(void *, Uint8 *stream, int len) {
  Sint16 *buf = (Sint16 *)stream;
  int n = len / 2;
  std::lock_guard<std::mutex> lk(beepState.mtx);
  for (int i = 0; i < n; i++) {
    if (beepState.samplesLeft > 0 && beepState.freq > 0) {
      double v = sin(beepState.phase) > 0 ? 0.18 : -0.18;
      buf[i] = (Sint16)(v * 32767);
      beepState.phase += 2.0 * M_PI * beepState.freq / 44100.0;
      beepState.samplesLeft--;
    } else buf[i] = 0;
  }
}
void beep(int freq, int ms) {
  if (muted) return;
  std::lock_guard<std::mutex> lk(beepState.mtx);
  beepState.freq = freq;
  beepState.samplesLeft = 44100 * ms / 1000;
  beepState.phase = 0;
}

uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ============================================================
// Capa de red real (Windows): WiFi (WLAN API), ping/portscan/
// http/dns (Winsock), traceroute (ICMP), WoL (UDP broadcast),
// barrido de dispositivos (ARP), perfiles guardados (netsh).
// Todo corre en hilos en segundo plano; cada herramienta expone
// un vector de lineas de resultado + un flag "busy".
// ============================================================
#ifdef _WIN32

struct ToolLog {
  std::mutex mtx;
  std::vector<std::pair<std::string, uint32_t>> lines; // texto, color
  std::atomic<bool> busy{false};
  void clear() { std::lock_guard<std::mutex> l(mtx); lines.clear(); }
  void add(const std::string &s, uint32_t col) { std::lock_guard<std::mutex> l(mtx); lines.push_back({s, col}); }
  std::vector<std::pair<std::string, uint32_t>> snapshot() { std::lock_guard<std::mutex> l(mtx); return lines; }
};

bool winsockReady = false;
void ensureWinsock() {
  if (winsockReady) return;
  WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
  winsockReady = true;
}

// ---------- WiFi scan (WLAN API) ----------
struct WifiNet { std::string ssid; int rssi; int channel; std::string auth; bool connected; std::string bssid; };
std::mutex wifiMtx;
std::vector<WifiNet> wifiNets;
std::atomic<bool> wifiScanBusy{false};
std::string wifiCurSsid; int wifiCurRssi = 0; bool wifiConnected = false;

int chanFromFreq(ULONG kHz) {
  int mhz = (int)(kHz / 1000);
  if (mhz >= 2412 && mhz <= 2484) return (mhz == 2484) ? 14 : (mhz - 2407) / 5;
  if (mhz >= 5000) return (mhz - 5000) / 5;
  return 0;
}

void wifiScanThread() {
  wifiScanBusy = true;
  std::vector<WifiNet> out;
  HANDLE hClient = nullptr; DWORD ver = 0;
  if (WlanOpenHandle(2, nullptr, &ver, &hClient) == ERROR_SUCCESS) {
    PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
    if (WlanEnumInterfaces(hClient, nullptr, &ifList) == ERROR_SUCCESS) {
      for (DWORD i = 0; i < ifList->dwNumberOfItems; i++) {
        GUID g = ifList->InterfaceInfo[i].InterfaceGuid;
        WlanScan(hClient, &g, nullptr, nullptr, nullptr);
      }
      Sleep(2500); // dar tiempo a que el driver complete el scan
      for (DWORD i = 0; i < ifList->dwNumberOfItems; i++) {
        GUID g = ifList->InterfaceInfo[i].InterfaceGuid;
        PWLAN_BSS_LIST bssList = nullptr;
        if (WlanGetNetworkBssList(hClient, &g, nullptr, dot11_BSS_type_any, FALSE, nullptr, &bssList) == ERROR_SUCCESS) {
          for (DWORD b = 0; b < bssList->dwNumberOfItems; b++) {
            WLAN_BSS_ENTRY &e = bssList->wlanBssEntries[b];
            WifiNet n;
            n.ssid.assign((char *)e.dot11Ssid.ucSSID, e.dot11Ssid.uSSIDLength);
            if (n.ssid.empty()) n.ssid = "(oculta)";
            n.rssi = e.lRssi;
            n.channel = chanFromFreq(e.ulChCenterFrequency);
            char bssidBuf[32];
            snprintf(bssidBuf, sizeof(bssidBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     e.dot11Bssid[0], e.dot11Bssid[1], e.dot11Bssid[2],
                     e.dot11Bssid[3], e.dot11Bssid[4], e.dot11Bssid[5]);
            n.bssid = bssidBuf;
            n.connected = false;
            n.auth = "?";
            out.push_back(n);
          }
          WlanFreeMemory(bssList);
        }
        // estado actual de conexion
        PWLAN_CONNECTION_ATTRIBUTES connAttr = nullptr; DWORD sz = 0; WLAN_OPCODE_VALUE_TYPE ot;
        if (WlanQueryInterface(hClient, &g, wlan_intf_opcode_current_connection, nullptr, &sz, (PVOID *)&connAttr, &ot) == ERROR_SUCCESS) {
          std::string cur((char *)connAttr->wlanAssociationAttributes.dot11Ssid.ucSSID,
                           connAttr->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
          wifiCurSsid = cur;
          wifiCurRssi = (int)connAttr->wlanAssociationAttributes.wlanSignalQuality; // 0-100
          wifiConnected = (connAttr->isState == wlan_interface_state_connected);
          WlanFreeMemory(connAttr);
        }
      }
      WlanFreeMemory(ifList);
    }
    WlanCloseHandle(hClient, nullptr);
  }
  // detectar duplicados de SSID (gemelos)
  std::sort(out.begin(), out.end(), [](const WifiNet &a, const WifiNet &b) { return a.rssi > b.rssi; });
  { std::lock_guard<std::mutex> l(wifiMtx); wifiNets = out; }
  wifiScanBusy = false;
}
void wifiScanStart() {
  ensureWinsock();
  std::thread(wifiScanThread).detach();
}

// ---------- Info de red (adaptador activo) ----------
struct NetInfo { std::string localIp, gateway, dns, mac, subnetMask; bool ok = false; };
NetInfo getNetInfo() {
  NetInfo info;
  ULONG sz = 0;
  GetAdaptersInfo(nullptr, &sz);
  std::vector<uint8_t> buf(sz);
  PIP_ADAPTER_INFO p = (PIP_ADAPTER_INFO)buf.data();
  if (GetAdaptersInfo(p, &sz) == ERROR_SUCCESS) {
    for (PIP_ADAPTER_INFO a = p; a; a = a->Next) {
      std::string ip = a->IpAddressList.IpAddress.String;
      if (ip != "0.0.0.0" && (a->Type == MIB_IF_TYPE_ETHERNET || a->Type == IF_TYPE_IEEE80211)) {
        info.localIp = ip;
        info.gateway = a->GatewayList.IpAddress.String;
        info.subnetMask = a->IpAddressList.IpMask.String;
        char mac[32]; snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
          a->Address[0], a->Address[1], a->Address[2], a->Address[3], a->Address[4], a->Address[5]);
        info.mac = mac;
        info.ok = true;
        break;
      }
    }
  }
  FIXED_INFO fi; ULONG fiSz = sizeof(fi);
  if (GetNetworkParams(&fi, &fiSz) == ERROR_SUCCESS) info.dns = fi.DnsServerList.IpAddress.String;
  return info;
}

// ---------- Ping (TCP connect timing, igual criterio que el firmware) ----------
ToolLog pingLog;
void pingThread(std::string host, int port) {
  ensureWinsock();
  pingLog.busy = true;
  for (int i = 0; i < 4 && pingLog.busy; i++) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
    bool resolved = false;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
      addr.sin_addr = ((sockaddr_in *)res->ai_addr)->sin_addr;
      freeaddrinfo(res); resolved = true;
    }
    if (!resolved) { pingLog.add("no se pudo resolver " + host, RUST); closesocket(s); break; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    uint32_t t0 = nowMs();
    connect(s, (sockaddr *)&addr, sizeof(addr));
    fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
    timeval tv{1, 500000};
    int sel = select(0, nullptr, &wf, nullptr, &tv);
    uint32_t dt = nowMs() - t0;
    char line[96];
    if (sel > 0) {
      int err = 0; int len = sizeof(err);
      getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
      if (err == 0) { snprintf(line, sizeof(line), "%s:%d  conectado en %u ms", host.c_str(), port, dt); pingLog.add(line, SAGE); }
      else { snprintf(line, sizeof(line), "%s:%d  rechazado (%u ms)", host.c_str(), port, dt); pingLog.add(line, GOLD_LT); }
    } else { snprintf(line, sizeof(line), "%s:%d  sin respuesta (timeout)", host.c_str(), port); pingLog.add(line, RUST); }
    closesocket(s);
    Sleep(200);
  }
  pingLog.busy = false;
}
void pingStart(const std::string &host, int port) { pingLog.clear(); std::thread(pingThread, host, port).detach(); }

// ---------- Escaneo de puertos ----------
ToolLog portLog;
std::atomic<int> portFound{0};
void portScanThread(std::string host) {
  ensureWinsock();
  portLog.busy = true; portFound = 0;
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
  in_addr target{};
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
    target = ((sockaddr_in *)res->ai_addr)->sin_addr; freeaddrinfo(res);
  } else { portLog.add("no se pudo resolver " + host, RUST); portLog.busy = false; return; }
  static const int common[] = {21,22,23,25,53,80,110,135,139,143,443,445,587,993,995,1433,3306,3389,5432,5900,8080,8443};
  for (int port : common) {
    if (!portLog.busy) break;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr = target;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    connect(s, (sockaddr *)&addr, sizeof(addr));
    fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
    timeval tv{0, 300000};
    if (select(0, nullptr, &wf, nullptr, &tv) > 0) {
      int err = 0; int len = sizeof(err);
      getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
      if (err == 0) {
        char line[64]; snprintf(line, sizeof(line), "puerto %d  ABIERTO", port);
        portLog.add(line, SAGE); portFound++;
      }
    }
    closesocket(s);
  }
  char sum[64]; snprintf(sum, sizeof(sum), "listo: %d puertos abiertos de %zu revisados", portFound.load(), sizeof(common)/sizeof(int));
  portLog.add(sum, GOLD_LT);
  portLog.busy = false;
}
void portScanStart(const std::string &host) { portLog.clear(); std::thread(portScanThread, host).detach(); }

// ---------- Traceroute (ICMP real con TTL creciente) ----------
ToolLog traceLog;
void traceThread(std::string host) {
  ensureWinsock();
  traceLog.busy = true;
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
    traceLog.add("no se pudo resolver " + host, RUST); traceLog.busy = false; return;
  }
  IPAddr dest = ((sockaddr_in *)res->ai_addr)->sin_addr.S_un.S_addr;
  freeaddrinfo(res);
  HANDLE hIcmp = IcmpCreateFile();
  if (hIcmp == INVALID_HANDLE_VALUE) { traceLog.add("no se pudo abrir ICMP", RUST); traceLog.busy = false; return; }
  char sendBuf[32] = "7toolsbandit";
  DWORD replySz = sizeof(ICMP_ECHO_REPLY) + sizeof(sendBuf) + 8;
  std::vector<uint8_t> replyBuf(replySz);
  for (int ttl = 1; ttl <= 30 && traceLog.busy; ttl++) {
    IP_OPTION_INFORMATION opt{}; opt.Ttl = (UCHAR)ttl;
    DWORD r = IcmpSendEcho(hIcmp, dest, sendBuf, (WORD)strlen(sendBuf),
                            &opt, replyBuf.data(), replySz, 1200);
    char line[96];
    if (r > 0) {
      PICMP_ECHO_REPLY rep = (PICMP_ECHO_REPLY)replyBuf.data();
      in_addr a; a.S_un.S_addr = rep->Address;
      snprintf(line, sizeof(line), "%2d  %s  %lu ms", ttl, inet_ntoa(a), rep->RoundTripTime);
      traceLog.add(line, rep->Status == IP_SUCCESS ? SAGE : GOLD_LT);
      if (rep->Status == IP_SUCCESS) { traceLog.add("destino alcanzado", SAGE); break; }
    } else {
      snprintf(line, sizeof(line), "%2d  * (sin respuesta)", ttl);
      traceLog.add(line, INK_DIM);
    }
  }
  IcmpCloseHandle(hIcmp);
  traceLog.busy = false;
}
void traceStart(const std::string &host) { traceLog.clear(); std::thread(traceThread, host).detach(); }

// ---------- Cliente HTTP ----------
ToolLog httpLog;
void httpThread(std::string url) {
  ensureWinsock();
  httpLog.busy = true;
  bool https = url.rfind("https://", 0) == 0;
  std::string rest = url.substr(https ? 8 : (url.rfind("http://", 0) == 0 ? 7 : 0));
  size_t slash = rest.find('/');
  std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
  int port = https ? 443 : 80;
  size_t colon = hostport.find(':');
  std::string host = hostport;
  if (colon != std::string::npos) { host = hostport.substr(0, colon); port = atoi(hostport.c_str() + colon + 1); }
  if (https) { httpLog.add("HTTPS no soportado en esta build (solo HTTP). Probando igual por el puerto 80...", GOLD_LT); port = 80; }
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  char portStr[8]; snprintf(portStr, sizeof(portStr), "%d", port);
  if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
    httpLog.add("no se pudo resolver " + host, RUST); httpLog.busy = false; return;
  }
  SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  uint32_t t0 = nowMs();
  if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
    httpLog.add("no se pudo conectar", RUST); closesocket(s); freeaddrinfo(res); httpLog.busy = false; return;
  }
  uint32_t tConn = nowMs() - t0;
  freeaddrinfo(res);
  std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                     "\r\nUser-Agent: SevenToolsBandit-PC/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
  send(s, req.c_str(), (int)req.size(), 0);
  char buf[2048]; int total = 0; std::string head;
  uint32_t deadline = nowMs() + 7000; bool gotStatus = false;
  while (nowMs() < deadline) {
    int n = recv(s, buf, sizeof(buf) - 1, 0);
    if (n <= 0) break;
    buf[n] = 0; total += n;
    if (!gotStatus) {
      head += buf;
      size_t eol = head.find('\r');
      if (eol != std::string::npos) {
        char line[128]; snprintf(line, sizeof(line), "conexion %u ms  %s", tConn, head.substr(0, std::min(eol, (size_t)100)).c_str());
        httpLog.add(line, SAGE); gotStatus = true;
      }
    }
    if (total > 4000) break;
  }
  if (!gotStatus) httpLog.add("sin respuesta del servidor", RUST);
  char sz[48]; snprintf(sz, sizeof(sz), "%d bytes recibidos", total); httpLog.add(sz, INK_DIM);
  closesocket(s);
  httpLog.busy = false;
}
void httpStart(const std::string &url) { httpLog.clear(); std::thread(httpThread, url).detach(); }

// ---------- DNS (resolucion real con getaddrinfo) ----------
ToolLog dnsLog;
void dnsThread(std::string host) {
  ensureWinsock();
  dnsLog.busy = true;
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
  uint32_t t0 = nowMs();
  int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
  uint32_t dt = nowMs() - t0;
  if (rc != 0 || !res) { dnsLog.add("no se pudo resolver (" + std::to_string(rc) + ")", RUST); dnsLog.busy = false; return; }
  int n = 0;
  for (addrinfo *p = res; p; p = p->ai_next) {
    char ipStr[64];
    inet_ntop(AF_INET, &((sockaddr_in *)p->ai_addr)->sin_addr, ipStr, sizeof(ipStr));
    char line[96]; snprintf(line, sizeof(line), "A    %s  (%u ms)", ipStr, dt);
    dnsLog.add(line, SAGE); n++;
  }
  freeaddrinfo(res);
  if (n == 0) dnsLog.add("sin registros A", RUST);
  dnsLog.busy = false;
}
void dnsStart(const std::string &host) { dnsLog.clear(); std::thread(dnsThread, host).detach(); }

// ---------- Wake on LAN ----------
std::string wolSend(const std::string &macStr) {
  ensureWinsock();
  uint8_t mac[6]; int v[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &v[0],&v[1],&v[2],&v[3],&v[4],&v[5]) != 6 &&
      sscanf(macStr.c_str(), "%x-%x-%x-%x-%x-%x", &v[0],&v[1],&v[2],&v[3],&v[4],&v[5]) != 6)
    return "MAC invalida";
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, mac, 6);
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  BOOL bOpt = TRUE; setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&bOpt, sizeof(bOpt));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(9); addr.sin_addr.s_addr = INADDR_BROADCAST;
  int r = sendto(s, (char *)pkt, sizeof(pkt), 0, (sockaddr *)&addr, sizeof(addr));
  closesocket(s);
  return r > 0 ? "paquete magico enviado" : "fallo al enviar";
}

// ---------- Barrido de dispositivos (ping sweep + tabla ARP) ----------
ToolLog devLog;
void devScanThread(std::string baseIp) {
  ensureWinsock();
  devLog.busy = true;
  size_t lastDot = baseIp.rfind('.');
  std::string prefix = baseIp.substr(0, lastDot + 1);
  HANDLE hIcmp = IcmpCreateFile();
  int found = 0;
  for (int i = 1; i <= 254 && devLog.busy; i++) {
    std::string ip = prefix + std::to_string(i);
    IPAddr dest = inet_addr(ip.c_str());
    char sendBuf[8] = "ping";
    uint8_t replyBuf[sizeof(ICMP_ECHO_REPLY) + 16];
    DWORD r = IcmpSendEcho(hIcmp, dest, sendBuf, 4, nullptr, replyBuf, sizeof(replyBuf), 150);
    if (r > 0) {
      PICMP_ECHO_REPLY rep = (PICMP_ECHO_REPLY)replyBuf;
      if (rep->Status == IP_SUCCESS) {
        char line[64]; snprintf(line, sizeof(line), "%s  activo (%lu ms)", ip.c_str(), rep->RoundTripTime);
        devLog.add(line, SAGE); found++;
      }
    }
  }
  IcmpCloseHandle(hIcmp);
  char sum[48]; snprintf(sum, sizeof(sum), "listo: %d dispositivos activos", found);
  devLog.add(sum, GOLD_LT);
  devLog.busy = false;
}
void devScanStart(const std::string &baseIp) { devLog.clear(); std::thread(devScanThread, baseIp).detach(); }

// ---------- Perfiles WiFi guardados (netsh) ----------
std::vector<std::string> wifiSavedProfiles() {
  std::vector<std::string> out;
  FILE *p = _popen("netsh wlan show profiles", "r");
  if (!p) return out;
  char line[512];
  while (fgets(line, sizeof(line), p)) {
    std::string s(line);
    size_t pos = s.find(" : ");
    if (s.find("Perfil de todos los usuarios") != std::string::npos ||
        s.find("All User Profile") != std::string::npos) {
      if (pos != std::string::npos) {
        std::string name = s.substr(pos + 3);
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) name.pop_back();
        out.push_back(name);
      }
    }
  }
  _pclose(p);
  return out;
}
bool wifiForget(const std::string &ssid) {
  std::string cmd = "netsh wlan delete profile name=\"" + ssid + "\"";
  return _pclose(_popen(cmd.c_str(), "r")) >= 0;
}

#endif // _WIN32

// ============================================================
// Herramientas de logica pura (sin red): subred, conversor IP,
// generador de claves, validador de rango.
// ============================================================
bool ipToU32(const std::string &s, uint32_t &out) {
  int a, b, c, d;
  if (sscanf(s.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
  out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
  return true;
}
std::string u32ToIp(uint32_t v) {
  char b[20]; snprintf(b, sizeof(b), "%u.%u.%u.%u", (v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255);
  return b;
}
struct SubnetResult { uint32_t network, broadcast, first, last, mask, hosts; bool ok; };
SubnetResult subnetCalc(const std::string &ipStr, int prefix) {
  SubnetResult r{}; r.ok = false;
  uint32_t ip;
  if (!ipToU32(ipStr, ip) || prefix < 0 || prefix > 32) return r;
  uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  r.mask = mask;
  r.network = ip & mask;
  r.broadcast = r.network | ~mask;
  r.hosts = prefix >= 31 ? 0 : (r.broadcast - r.network - 1);
  r.first = prefix >= 31 ? r.network : r.network + 1;
  r.last = prefix >= 31 ? r.broadcast : r.broadcast - 1;
  r.ok = true;
  return r;
}
std::string genPassword(int len, bool upper, bool digits, bool symbols) {
  std::string pool = "abcdefghijklmnopqrstuvwxyz";
  if (upper) pool += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (digits) pool += "0123456789";
  if (symbols) pool += "!@#$%^&*-_=+?";
  std::string out;
  for (int i = 0; i < len; i++) out += pool[rand() % pool.size()];
  return out;
}
struct RangeCheck { bool ok, inside, isNet, isBc; std::string net, bc, first, last; };
RangeCheck rangeValidate(const std::string &ipStr, const std::string &netStr, int prefix) {
  RangeCheck r{}; r.ok = false;
  uint32_t ip, net;
  if (!ipToU32(ipStr, ip) || !ipToU32(netStr, net) || prefix < 0 || prefix > 32) return r;
  uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  uint32_t network = net & mask, bc = network | ~mask;
  r.inside = (ip & mask) == network;
  r.isNet = ip == network;
  r.isBc = ip == bc;
  r.net = u32ToIp(network); r.bc = u32ToIp(bc);
  r.first = u32ToIp(prefix >= 31 ? network : network + 1);
  r.last = u32ToIp(prefix >= 31 ? bc : bc - 1);
  r.ok = true;
  return r;
}

// ============================================================
// Explorador de archivos local (reemplaza la microSD: usa una
// carpeta "cardputer_sd" junto al ejecutable).
// ============================================================
#include <filesystem>
namespace fs = std::filesystem;
std::string sdRoot() {
  static std::string root;
  if (root.empty()) {
    root = "cardputer_sd";
    fs::create_directories(root);
  }
  return root;
}

// ============================================================
// Estructura de pantallas y menu (fiel a las 7 categorias del
// firmware original; herramientas sin equivalente real en PC
// se muestran con un aviso en vez de datos inventados).
// ============================================================
enum Screen {
  SPLASH, WELCOME, LAUNCHER, SUBMENU, INSTR,
  WIFI_SCAN, NET_INFO, WIFI_SAVED, WIFI_TWIN,
  PING_SCR, PORTSCAN, TRACE_SCR, HTTP_SCR, DNS_SCR,
  CHAN_VIEW, DEV_COUNT, SIG_LIVE, PKT_UNAVAIL,
  SUBNET, WOL_SCR, PASSGEN, RANGE_SCR, IPCONV,
  HID_COMBO, HID_UNAVAIL,
  FILE_EXP, FILE_VIEW, SD_INFO,
  FILESRV
};

struct Cat { const char *name; uint8_t first, count; uint32_t col; };
struct Item { const char *name; Screen scr; };

const Item TOOLS[] = {
  // WIFI
  {"WiFi Scan", WIFI_SCAN}, {"Info de red", NET_INFO}, {"Redes guardadas", WIFI_SAVED}, {"Gemelos (SSID duplicado)", WIFI_TWIN},
  // SONDEO
  {"Ping TCP", PING_SCR}, {"Escaneo de puertos", PORTSCAN}, {"Traceroute", TRACE_SCR}, {"Cliente HTTP", HTTP_SCR}, {"DNS", DNS_SCR},
  // MONITOR
  {"Canales WiFi", CHAN_VIEW}, {"Dispositivos en una red", DEV_COUNT}, {"Senal en vivo", SIG_LIVE}, {"Monitor de paquetes", PKT_UNAVAIL},
  // RED LOCAL
  {"Calculadora de subred", SUBNET}, {"Wake on LAN", WOL_SCR}, {"Generador de claves", PASSGEN}, {"Validador de rango", RANGE_SCR}, {"Conversor de IP", IPCONV},
  // USB HID
  {"Combos de teclado (a esta PC)", HID_COMBO}, {"Teclado/Mouse USB real", HID_UNAVAIL},
  // ARCHIVOS
  {"Explorador (cardputer_sd)", FILE_EXP}, {"Espacio en disco", SD_INFO},
  // SERVIDOR
  {"Servidor de archivos (en mi red)", FILESRV},
};
const Cat CATS[] = {
  {"WIFI",       0, 4, DUSK},
  {"SONDEO",     4, 5, SAGE},
  {"MONITOR",    9, 4, RUST},
  {"RED LOCAL", 13, 5, BANDIT_GOLD},
  {"USB HID",   18, 2, PLUM},
  {"ARCHIVOS",  20, 2, GOLD_LT},
  {"SERVIDOR",  22, 1, DUSK},
};
constexpr int NCAT = sizeof(CATS) / sizeof(Cat);

Screen screen = SPLASH;
int launchSel = 0, subSel = 0, curCat = 0;
Screen instrFrom = LAUNCHER;
uint32_t splashStart = 0;
constexpr uint32_t SPLASH_MS = 1800;

// toast
std::string toastMsg; uint32_t toastUntil = 0; uint32_t toastCol = SAGE;
void toast(const std::string &s, uint32_t col) { toastMsg = s; toastCol = col; toastUntil = nowMs() + 2200; }

// entrada de texto generica
struct TextInput { bool active = false; std::string title; std::string buf; Screen owner; } tin;
void openTextInput(const std::string &title, const std::string &def, Screen owner) {
  tin.active = true; tin.title = title; tin.buf = def; tin.owner = owner;
}

// estado por herramienta (host/puerto/etc para los formularios)
std::string hostBuf = "8.8.8.8", portBuf = "80", urlBuf = "example.com", macBuf = "AA:BB:CC:DD:EE:FF";
std::string subnetIpBuf = "192.168.1.10", subnetPrefixBuf = "24";
std::string passLenBuf = "16"; bool passUpper = true, passDigits = true, passSymbols = true;
std::string rangeIpBuf = "192.168.1.50", rangeNetBuf = "192.168.1.0", rangePrefixBuf = "24";
std::string ipConvBuf = "192.168.1.1";
int formStep = 0; // para formularios con varios campos

// explorador de archivos
std::string curDir;
std::vector<fs::directory_entry> dirEntries;
int fileSel = 0; std::string fileViewPath; std::string fileViewContent;

void refreshDir() {
  dirEntries.clear();
  if (curDir.empty()) curDir = sdRoot();
  try {
    for (auto &e : fs::directory_iterator(curDir)) dirEntries.push_back(e);
    std::sort(dirEntries.begin(), dirEntries.end(), [](auto &a, auto &b) {
      if (a.is_directory() != b.is_directory()) return a.is_directory() > b.is_directory();
      return a.path().filename().string() < b.path().filename().string();
    });
  } catch (...) {}
  fileSel = 0;
}

#ifdef _WIN32
// servidor de archivos HTTP simple (sirve curDir/sdRoot por la red local)
std::atomic<bool> fsRunning{false};
SOCKET fsListenSock = INVALID_SOCKET;
std::thread fsThread;
std::vector<std::string> fsAccessLog;
std::mutex fsLogMtx;
void fsLogAdd(const std::string &s) { std::lock_guard<std::mutex> l(fsLogMtx); fsAccessLog.push_back(s); if (fsAccessLog.size() > 50) fsAccessLog.erase(fsAccessLog.begin()); }

void handleFsClient(SOCKET c) {
  char buf[2048] = {0};
  int n = recv(c, buf, sizeof(buf) - 1, 0);
  if (n <= 0) { closesocket(c); return; }
  std::string req(buf, n);
  std::string path = "/";
  size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
  if (sp1 != std::string::npos && sp2 != std::string::npos) path = req.substr(sp1 + 1, sp2 - sp1 - 1);
  if (path == "/") {
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><title>Seven Tools Bandit - Servidor</title></head><body style='font-family:monospace;background:#14111a;color:#ebdec8'>";
    html += "<h2>Carpeta compartida (cardputer_sd)</h2><ul>";
    try {
      for (auto &e : fs::directory_iterator(sdRoot())) {
        std::string nm = e.path().filename().string();
        html += "<li><a style='color:#f5cd82' href='/dl?f=" + nm + "'>" + nm + "</a></li>";
      }
    } catch (...) {}
    html += "</ul></body></html>";
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                        std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
    send(c, resp.c_str(), (int)resp.size(), 0);
    fsLogAdd("GET / (listado)");
  } else if (path.rfind("/dl?f=", 0) == 0) {
    std::string fn = path.substr(6);
    fs::path full = fs::path(sdRoot()) / fn;
    std::ifstream f(full, std::ios::binary);
    if (f) {
      std::ostringstream ss; ss << f.rdbuf(); std::string data = ss.str();
      std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\"" + fn +
                          "\"\r\nContent-Length: " + std::to_string(data.size()) + "\r\nConnection: close\r\n\r\n";
      send(c, resp.c_str(), (int)resp.size(), 0);
      send(c, data.data(), (int)data.size(), 0);
      fsLogAdd("GET " + fn + " (descarga, " + std::to_string(data.size()) + " bytes)");
    } else {
      std::string resp = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
      send(c, resp.c_str(), (int)resp.size(), 0);
    }
  } else {
    std::string resp = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
    send(c, resp.c_str(), (int)resp.size(), 0);
  }
  closesocket(c);
}
void fsServerLoop() {
  while (fsRunning) {
    fd_set rf; FD_ZERO(&rf); FD_SET(fsListenSock, &rf);
    timeval tv{0, 200000};
    if (select(0, &rf, nullptr, nullptr, &tv) > 0) {
      SOCKET c = accept(fsListenSock, nullptr, nullptr);
      if (c != INVALID_SOCKET) std::thread(handleFsClient, c).detach();
    }
  }
}
bool fsStart() {
  ensureWinsock();
  fsListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(8080); addr.sin_addr.s_addr = INADDR_ANY;
  BOOL opt = TRUE; setsockopt(fsListenSock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
  if (bind(fsListenSock, (sockaddr *)&addr, sizeof(addr)) != 0) return false;
  listen(fsListenSock, 8);
  fsRunning = true;
  fsThread = std::thread(fsServerLoop);
  return true;
}
void fsStop() {
  if (!fsRunning) return;
  fsRunning = false;
  if (fsThread.joinable()) fsThread.join();
  closesocket(fsListenSock);
}
#endif

// ============================================================
// Utilidades de dibujo compartidas
// ============================================================
void drawHeader(const std::string &title, uint32_t col) {
  canvas.fillRect(0, 0, W, 16, BG1);
  canvas.setTextDatum(middle_left); canvas.setTextSize(1); canvas.setTextColor(col); canvas.setBold(true);
  canvas.drawString(title, 6, 8);
  canvas.setBold(false);
  canvas.drawFastHLine(0, 16, W, METAL_D);
}
void drawFooter(const std::string &hint) {
  canvas.setTextDatum(bottom_left); canvas.setTextSize(1); canvas.setTextColor(INK_DIM);
  canvas.drawString(hint, 4, H - 2);
}
void drawToast() {
  if (nowMs() > toastUntil) return;
  canvas.fillRoundRect(10, H - 30, W - 20, 20, 4, BG2);
  canvas.drawRoundRect(10, H - 30, W - 20, 20, 4, toastCol);
  canvas.setTextDatum(middle_center); canvas.setTextColor(INK); canvas.setTextSize(1);
  canvas.drawString(toastMsg, W / 2, H - 20);
}
void drawTextInputOverlay() {
  if (!tin.active) return;
  canvas.fillRoundRect(10, 40, W - 20, 50, 6, BG2);
  canvas.drawRoundRect(10, 40, W - 20, 50, 6, BANDIT_GOLD);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(BANDIT_GOLD);
  canvas.drawString(tin.title, 16, 46);
  canvas.fillRoundRect(16, 60, W - 32, 16, 3, BG0);
  canvas.setTextColor(INK);
  std::string show = tin.buf + (((nowMs() / 400) % 2) ? "_" : " ");
  canvas.drawString(show, 20, 64);
  canvas.setTextColor(INK_DIM); canvas.drawString("ENTER confirmar   ` cancelar", 16, 80);
}
// (drawLogScreen vive mas abajo, junto a ToolLog, dentro del bloque _WIN32)

// ============================================================
// SPLASH / WELCOME / LAUNCHER / SUBMENU / INSTR
// ============================================================
void drawSplash() {
  canvas.fillScreen(BG0);
  uint32_t t = nowMs() - splashStart;
  float a = clampf(t / 600.0f, 0, 1);
  canvas.setTextDatum(middle_center); canvas.setTextSize(2); canvas.setTextColor(BANDIT_GOLD);
  canvas.drawString("SEVEN TOOLS", W / 2, H / 2 - 14);
  canvas.setTextSize(1); canvas.setTextColor(INK_DIM);
  canvas.drawString("of the Bandit - PC Edition", W / 2, H / 2 + 6);
  canvas.setTextColor(METAL);
  canvas.drawString("Powered By Shin-Lima_Kun", W / 2, H - 14);
  (void)a;
}
void drawWelcome() {
  canvas.fillScreen(BG0);
  canvas.setTextDatum(top_center); canvas.setTextSize(1); canvas.setTextColor(BANDIT_GOLD);
  canvas.drawString("Navaja suiza de sysadmin", W / 2, 14);
  canvas.setTextColor(INK_DIM);
  canvas.drawString("WiFi, red, puertos, subred, WoL,", W / 2, 30);
  canvas.drawString("archivos y servidor - version PC", W / 2, 42);
  canvas.setTextColor(SAGE);
  canvas.drawString("Las herramientas de red son REALES:", W / 2, 60);
  canvas.drawString("usan la tarjeta de red de esta PC.", W / 2, 72);
  canvas.setTextDatum(middle_center); canvas.setTextColor(GOLD_LT);
  if ((nowMs() / 500) % 2) canvas.drawString("pulsa cualquier tecla", W / 2, 100);
}
void drawList(bool sub) {
  canvas.fillScreen(BG0);
  if (!sub) {
    drawHeader("SEVEN TOOLS - MENU", BANDIT_GOLD);
    canvas.setTextDatum(middle_left); canvas.setTextSize(1);
    int y = 26;
    for (int i = 0; i < NCAT; i++) {
      bool selected = (i == launchSel);
      if (selected) canvas.fillRoundRect(4, y - 7, W - 8, 15, 3, BG2);
      canvas.setTextColor(selected ? GOLD_LT : INK);
      canvas.drawString(std::string(selected ? "> " : "  ") + CATS[i].name, 10, y);
      y += 16;
    }
    drawFooter("; . mover   ENTER elegir   i info");
  } else {
    drawHeader(CATS[curCat].name, CATS[curCat].col);
    canvas.setTextDatum(middle_left); canvas.setTextSize(1);
    int y = 24;
    for (int i = 0; i < CATS[curCat].count; i++) {
      const Item &it = TOOLS[CATS[curCat].first + i];
      bool selected = (i == subSel);
      if (selected) canvas.fillRoundRect(4, y - 6, W - 8, 13, 3, BG2);
      canvas.setTextColor(selected ? GOLD_LT : INK);
      canvas.drawString(std::string(selected ? "> " : "  ") + it.name, 8, y);
      y += 13;
    }
    drawFooter("` volver   ENTER abrir   i info");
  }
}
void drawInstr() {
  canvas.fillScreen(BG0);
  drawHeader("INSTRUCCIONES", BANDIT_GOLD);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  const char *lines[] = {
    "; . mueven el cursor",
    "ENTER elige / activa",
    "` (backtick) vuelve atras",
    "i muestra esta ayuda",
    "M silencia el sonido",
    "En formularios: escribe y",
    "ENTER confirma cada campo"
  };
  int y = 24;
  for (auto l : lines) { canvas.drawString(l, 8, y); y += 13; }
  drawFooter("` o i para volver");
}

// ============================================================
// WIFI
// ============================================================
#ifdef _WIN32
void drawWifiScan() {
  drawHeader("WIFI SCAN", DUSK);
  auto nets = [] { std::lock_guard<std::mutex> l(wifiMtx); return wifiNets; }();
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  int y = 20;
  if (wifiScanBusy) {
    canvas.setTextColor(GOLD_LT);
    canvas.drawString("escaneando redes cercanas...", 6, y);
  } else if (nets.empty()) {
    canvas.setTextColor(INK_DIM); canvas.drawString("sin resultados. pulsa R para escanear", 6, y);
  } else {
    for (size_t i = 0; i < nets.size() && i < 8; i++) {
      char line[80];
      snprintf(line, sizeof(line), "%-18s ch%-2d %ddBm", nets[i].ssid.substr(0,18).c_str(), nets[i].channel, nets[i].rssi);
      canvas.setTextColor(nets[i].rssi > -60 ? SAGE : (nets[i].rssi > -75 ? GOLD_LT : INK_DIM));
      canvas.drawString(line, 6, y);
      y += 12;
    }
  }
  drawFooter("R escanear   ` volver");
}
void drawNetInfo() {
  drawHeader("INFO DE RED", DUSK);
  NetInfo info = getNetInfo();
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  int y = 22;
  auto line = [&](const std::string &label, const std::string &val) {
    canvas.setTextColor(INK_DIM); canvas.drawString(label, 6, y);
    canvas.setTextColor(INK); canvas.drawString(val, 70, y);
    y += 14;
  };
  if (info.ok) {
    line("IP local:", info.localIp);
    line("Gateway:", info.gateway);
    line("Mascara:", info.subnetMask);
    line("DNS:", info.dns);
    line("MAC:", info.mac);
  } else {
    canvas.setTextColor(RUST); canvas.drawString("sin adaptador de red activo", 6, y);
  }
  drawFooter("` volver");
}
void drawWifiTwin() {
  drawHeader("GEMELOS (SSID duplicado)", DUSK);
  auto nets = [] { std::lock_guard<std::mutex> l(wifiMtx); return wifiNets; }();
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  int y = 20; bool any = false;
  for (size_t i = 0; i < nets.size(); i++) {
    for (size_t j = i + 1; j < nets.size(); j++) {
      if (nets[i].ssid == nets[j].ssid && nets[i].bssid != nets[j].bssid) {
        char line[80]; snprintf(line, sizeof(line), "%s  (%s / %s)", nets[i].ssid.c_str(), nets[i].bssid.c_str(), nets[j].bssid.c_str());
        canvas.setTextColor(RUST); canvas.drawString(line, 6, y); y += 12; any = true;
        break;
      }
    }
  }
  if (!any) { canvas.setTextColor(SAGE); canvas.drawString(nets.empty() ? "haz un WiFi Scan primero" : "sin gemelos detectados", 6, y); }
  drawFooter("` volver  (usa datos del ultimo WiFi Scan)");
}
void drawWifiSaved() {
  drawHeader("REDES GUARDADAS", DUSK);
  static std::vector<std::string> profiles;
  static bool loaded = false;
  if (!loaded) { profiles = wifiSavedProfiles(); loaded = true; }
  canvas.setTextDatum(middle_left); canvas.setTextSize(1);
  int y = 22;
  for (size_t i = 0; i < profiles.size() && i < 8; i++) {
    bool sel = (int)i == subSel;
    if (sel) canvas.fillRoundRect(4, y - 6, W - 8, 13, 3, BG2);
    canvas.setTextColor(sel ? GOLD_LT : INK);
    canvas.drawString(profiles[i], 8, y); y += 13;
  }
  if (profiles.empty()) { canvas.setTextColor(INK_DIM); canvas.drawString("sin perfiles guardados", 8, y); }
  drawFooter("; . mover   ENTER olvidar   ` volver");
}
#endif

// ============================================================
// SONDEO: ping, portscan, traceroute, http, dns
// ============================================================
#ifdef _WIN32
void drawLogScreen(const std::string &title, uint32_t titleCol, ToolLog &log, const std::string &hint) {
  drawHeader(title, titleCol);
  auto lines = log.snapshot();
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  int y = 20;
  int maxLines = 9;
  int start = (int)lines.size() > maxLines ? (int)lines.size() - maxLines : 0;
  for (int i = start; i < (int)lines.size(); i++) {
    canvas.setTextColor(lines[i].second);
    canvas.drawString(lines[i].first, 6, y);
    y += 11;
  }
  if (log.busy) {
    canvas.setTextColor(GOLD_LT);
    int dots = (nowMs() / 300) % 4;
    canvas.drawString(std::string("trabajando") + std::string(dots, '.'), 6, y);
  }
  drawFooter(hint);
}
void drawPing() { drawLogScreen("PING TCP a " + hostBuf + ":" + portBuf, SAGE, pingLog, "T host  ` volver"); }
void drawPortScan() { drawLogScreen("ESCANEO DE PUERTOS " + hostBuf, SAGE, portLog, "T host  ` volver"); }
void drawTrace() { drawLogScreen("TRACEROUTE a " + hostBuf, SAGE, traceLog, "T host  ` volver"); }
void drawHttp() { drawLogScreen("CLIENTE HTTP " + urlBuf, SAGE, httpLog, "T url  ` volver"); }
void drawDns() { drawLogScreen("DNS " + hostBuf, SAGE, dnsLog, "T host  ` volver"); }

// ============================================================
// MONITOR: canales, dispositivos, senal en vivo
// ============================================================
void drawChanView() {
  drawHeader("CANALES WIFI EN USO", RUST);
  auto nets = [] { std::lock_guard<std::mutex> l(wifiMtx); return wifiNets; }();
  int hist[15] = {0};
  for (auto &n : nets) if (n.channel >= 1 && n.channel <= 14) hist[n.channel]++;
  canvas.setTextDatum(bottom_center); canvas.setTextSize(1);
  int maxV = 1; for (int i = 1; i <= 14; i++) maxV = std::max(maxV, hist[i]);
  for (int ch = 1; ch <= 14; ch++) {
    int bh = (int)(hist[ch] * 70.0f / maxV);
    int x = 10 + (ch - 1) * 16;
    canvas.fillRect(x, 108 - bh, 10, bh, hist[ch] ? GOLD_LT : METAL_D);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(std::to_string(ch), x + 5, 120);
  }
  if (nets.empty()) { canvas.setTextDatum(top_left); canvas.setTextColor(INK_DIM); canvas.drawString("haz un WiFi Scan primero", 6, 24); }
  drawFooter("` volver  (datos del ultimo WiFi Scan)");
}
void drawDevCount() { drawLogScreen("DISPOSITIVOS EN " + hostBuf + ".0/24", RUST, devLog, "T red (ej 192.168.1.1)  ` volver"); }
void drawSigLive() {
  drawHeader("SENAL EN VIVO", RUST);
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  if (!wifiConnected) {
    canvas.setTextColor(RUST); canvas.drawString("no estas conectado a ninguna red WiFi", 6, 24);
  } else {
    canvas.setTextColor(INK); canvas.drawString("SSID: " + wifiCurSsid, 6, 24);
    canvas.setTextColor(SAGE); char l[48]; snprintf(l, sizeof(l), "Calidad de senal: %d%%", wifiCurRssi);
    canvas.drawString(l, 6, 40);
    int barW = (int)(wifiCurRssi * 2.0f);
    canvas.fillRoundRect(6, 56, 200, 14, 3, BG2);
    canvas.fillRoundRect(6, 56, std::max(4, barW), 14, 3, wifiCurRssi > 60 ? SAGE : (wifiCurRssi > 30 ? GOLD_LT : RUST));
  }
  drawFooter("` volver  (se actualiza solo)");
}
void drawPktUnavail() {
  drawHeader("MONITOR DE PAQUETES", RUST);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(RUST);
  canvas.drawString("No disponible en PC", 6, 26);
  canvas.setTextColor(INK_DIM);
  canvas.drawString("El modo promiscuo de WiFi es", 6, 44);
  canvas.drawString("exclusivo del chip del ESP32.", 6, 56);
  canvas.drawString("Una tarjeta WiFi normal de PC", 6, 70);
  canvas.drawString("no puede capturar trafico ajeno", 6, 82);
  canvas.drawString("sin un adaptador especial.", 6, 94);
  drawFooter("` volver");
}

// ============================================================
// RED LOCAL: subred, WoL, claves, rango, conversor IP
// ============================================================
void drawSubnet() {
  drawHeader("CALCULADORA DE SUBRED", BANDIT_GOLD);
  SubnetResult r = subnetCalc(subnetIpBuf, atoi(subnetPrefixBuf.c_str()));
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  int y = 20;
  canvas.setTextColor(INK); canvas.drawString("IP: " + subnetIpBuf + "/" + subnetPrefixBuf, 6, y); y += 14;
  if (r.ok) {
    canvas.setTextColor(SAGE);
    canvas.drawString("Red:        " + u32ToIp(r.network), 6, y); y += 12;
    canvas.drawString("Broadcast:  " + u32ToIp(r.broadcast), 6, y); y += 12;
    canvas.drawString("Rango:      " + u32ToIp(r.first) + " - " + u32ToIp(r.last), 6, y); y += 12;
    canvas.drawString("Mascara:    " + u32ToIp(r.mask), 6, y); y += 12;
    canvas.drawString("Hosts utiles: " + std::to_string(r.hosts), 6, y);
  } else { canvas.setTextColor(RUST); canvas.drawString("IP o prefijo invalido", 6, y); }
  drawFooter("I ip   P prefijo   ` volver");
}
void drawWol() {
  drawHeader("WAKE ON LAN", BANDIT_GOLD);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  canvas.drawString("MAC destino: " + macBuf, 6, 24);
  drawFooter("T editar MAC   ENTER enviar   ` volver");
}
void drawPassGen() {
  drawHeader("GENERADOR DE CLAVES", BANDIT_GOLD);
  static std::string cur;
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(GOLD_LT); canvas.setTextSize(2);
  if (cur.empty()) cur = genPassword(atoi(passLenBuf.c_str()), passUpper, passDigits, passSymbols);
  canvas.drawString(cur, 6, 28); canvas.setTextSize(1);
  canvas.setTextColor(INK_DIM);
  canvas.drawString("largo " + passLenBuf + "  MAY:" + (passUpper?"on":"off") + "  NUM:" + (passDigits?"on":"off") + "  SIM:" + (passSymbols?"on":"off"), 6, 60);
  drawFooter("ENTER nueva  U/N/S alternar  L largo");
  static bool regen = false;
  if (regen) { cur = genPassword(atoi(passLenBuf.c_str()), passUpper, passDigits, passSymbols); regen = false; }
}
void drawRange() {
  drawHeader("VALIDADOR DE RANGO", BANDIT_GOLD);
  RangeCheck r = rangeValidate(rangeIpBuf, rangeNetBuf, atoi(rangePrefixBuf.c_str()));
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  int y = 20;
  canvas.drawString("IP: " + rangeIpBuf, 6, y); y += 12;
  canvas.drawString("Red: " + rangeNetBuf + "/" + rangePrefixBuf, 6, y); y += 14;
  if (r.ok) {
    canvas.setTextColor(r.inside ? SAGE : RUST);
    canvas.drawString(r.inside ? "DENTRO del rango" : "FUERA del rango", 6, y); y += 14;
    canvas.setTextColor(INK_DIM);
    if (r.isNet) canvas.drawString("(es la direccion de red)", 6, y);
    else if (r.isBc) canvas.drawString("(es la direccion de broadcast)", 6, y);
    else canvas.drawString("rango util: " + r.first + " - " + r.last, 6, y);
  } else { canvas.setTextColor(RUST); canvas.drawString("datos invalidos", 6, y); }
  drawFooter("I ip   N red   P prefijo   ` volver");
}
void drawIpConv() {
  drawHeader("CONVERSOR DE IP", BANDIT_GOLD);
  uint32_t v;
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  canvas.drawString("IP: " + ipConvBuf, 6, 22);
  if (ipToU32(ipConvBuf, v)) {
    char bin[40]; for (int i = 0; i < 32; i++) bin[i] = ((v >> (31 - i)) & 1) ? '1' : '0'; bin[32] = 0;
    char hexs[16]; snprintf(hexs, sizeof(hexs), "0x%08X", v);
    canvas.setTextColor(SAGE);
    canvas.drawString("Decimal: " + std::to_string(v), 6, 40);
    canvas.drawString(std::string("Hex:     ") + hexs, 6, 54);
    canvas.setTextSize(1);
    canvas.drawString(std::string("Binario: ") + std::string(bin, 16), 6, 68);
    canvas.drawString(std::string("         ") + std::string(bin + 16, 16), 6, 80);
  } else { canvas.setTextColor(RUST); canvas.drawString("formato invalido", 6, 40); }
  drawFooter("T editar IP   ` volver");
}
#endif

// ============================================================
// USB HID: en una PC no se puede ser un dispositivo USB de uno
// mismo. Los "combos" se reenvian a esta misma PC via SendInput
// (equivalente funcional, no es HID real).
// ============================================================
#ifdef _WIN32
struct Combo { const char *name; WORD mods[3]; WORD key; };
const Combo COMBOS[] = {
  {"Win+L  bloquear PC",        {VK_LWIN, 0, 0}, 'L'},
  {"Win+D  escritorio",         {VK_LWIN, 0, 0}, 'D'},
  {"Win+R  ejecutar",           {VK_LWIN, 0, 0}, 'R'},
  {"Win+E  explorador",         {VK_LWIN, 0, 0}, 'E'},
  {"Win+Tab  vistas",           {VK_LWIN, 0, 0}, VK_TAB},
  {"Win+Shift+S  captura",      {VK_LWIN, VK_SHIFT, 0}, 'S'},
  {"Ctrl+Shift+Esc  tareas",    {VK_CONTROL, VK_SHIFT, 0}, VK_ESCAPE},
  {"Alt+Tab",                   {VK_MENU, 0, 0}, VK_TAB},
  {"Alt+F4  cerrar",            {VK_MENU, 0, 0}, VK_F4},
  {"Ctrl+C  copiar",            {VK_CONTROL, 0, 0}, 'C'},
  {"Ctrl+V  pegar",             {VK_CONTROL, 0, 0}, 'V'},
  {"Ctrl+Z  deshacer",          {VK_CONTROL, 0, 0}, 'Z'},
  {"Ctrl+S  guardar",           {VK_CONTROL, 0, 0}, 'S'},
};
constexpr int NCOMBO = sizeof(COMBOS) / sizeof(Combo);
int comboSel = 0;
void sendCombo(const Combo &c) {
  std::vector<INPUT> ins;
  auto addKey = [&](WORD vk, bool down) {
    INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk; in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    ins.push_back(in);
  };
  for (int i = 0; i < 3; i++) if (c.mods[i]) addKey(c.mods[i], true);
  addKey(c.key, true); addKey(c.key, false);
  for (int i = 2; i >= 0; i--) if (c.mods[i]) addKey(c.mods[i], false);
  SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
}
void drawHidCombo() {
  drawHeader("COMBOS DE TECLADO (a esta PC)", PLUM);
  canvas.setTextDatum(middle_left); canvas.setTextSize(1);
  int y = 22;
  for (int i = 0; i < NCOMBO && i < 8; i++) {
    bool sel = i == comboSel;
    if (sel) canvas.fillRoundRect(4, y - 6, W - 8, 12, 2, BG2);
    canvas.setTextColor(sel ? GOLD_LT : INK);
    canvas.drawString(COMBOS[i].name, 8, y); y += 12;
  }
  drawFooter("; . mover  ENTER enviar  ` volver");
}
void drawHidUnavail() {
  drawHeader("TECLADO/MOUSE USB REAL", PLUM);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(RUST);
  canvas.drawString("No disponible en PC", 6, 26);
  canvas.setTextColor(INK_DIM);
  canvas.drawString("El Cardputer se anuncia como un", 6, 44);
  canvas.drawString("teclado/mouse USB ante OTRO equipo.", 6, 56);
  canvas.drawString("Una PC no puede declararse a si", 6, 70);
  canvas.drawString("misma como dispositivo USB.", 6, 82);
  canvas.setTextColor(GOLD_LT);
  canvas.drawString("Usa 'Combos de teclado' para algo", 6, 98);
  canvas.drawString("equivalente en esta misma PC.", 6, 110);
  drawFooter("` volver");
}

// ============================================================
// ARCHIVOS: explorador local + vista previa de texto + espacio
// ============================================================
void drawFileExp() {
  drawHeader("EXPLORADOR: " + fs::path(curDir).filename().string(), GOLD_LT);
  canvas.setTextDatum(middle_left); canvas.setTextSize(1);
  int y = 22;
  for (size_t i = 0; i < dirEntries.size() && i < 8; i++) {
    bool sel = (int)i == fileSel;
    std::string nm = dirEntries[i].path().filename().string();
    if (dirEntries[i].is_directory()) nm += "/";
    if (sel) canvas.fillRoundRect(4, y - 6, W - 8, 12, 2, BG2);
    canvas.setTextColor(sel ? GOLD_LT : (dirEntries[i].is_directory() ? SAGE : INK));
    canvas.drawString(nm.substr(0, 34), 8, y); y += 12;
  }
  if (dirEntries.empty()) { canvas.setTextColor(INK_DIM); canvas.drawString("(carpeta vacia)", 8, y); }
  drawFooter("; . mover  ENTER abrir  ` subir/salir");
}
void drawFileView() {
  drawHeader("VISTA: " + fs::path(fileViewPath).filename().string(), GOLD_LT);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  std::istringstream iss(fileViewContent);
  std::string line; int y = 20;
  while (std::getline(iss, line) && y < H - 10) { canvas.drawString(line.substr(0, 36), 6, y); y += 11; }
  drawFooter("` volver");
}
void drawSdInfo() {
  drawHeader("ESPACIO EN DISCO", GOLD_LT);
  canvas.setTextDatum(top_left); canvas.setTextSize(1); canvas.setTextColor(INK);
  try {
    auto space = fs::space(sdRoot());
    uint64_t totalGB = space.capacity / (1024ull*1024*1024);
    uint64_t freeGB = space.free / (1024ull*1024*1024);
    uint64_t usedGB = totalGB - freeGB;
    canvas.drawString("Carpeta: " + fs::absolute(sdRoot()).string(), 6, 24);
    canvas.setTextColor(SAGE); canvas.drawString("Libre:  " + std::to_string(freeGB) + " GB", 6, 46);
    canvas.setTextColor(RUST); canvas.drawString("Usado:  " + std::to_string(usedGB) + " GB", 6, 60);
    canvas.setTextColor(INK); canvas.drawString("Total:  " + std::to_string(totalGB) + " GB", 6, 74);
    int barW = (int)((double)usedGB / std::max<uint64_t>(1,totalGB) * (W - 20));
    canvas.fillRoundRect(10, 90, W - 20, 12, 3, BG2);
    canvas.fillRoundRect(10, 90, std::max(2, barW), 12, 3, RUST);
  } catch (...) { canvas.setTextColor(RUST); canvas.drawString("no se pudo leer el disco", 6, 30); }
  drawFooter("` volver");
}

// ============================================================
// SERVIDOR: servidor de archivos HTTP en la red local
// ============================================================
void drawFileSrv() {
  drawHeader("SERVIDOR DE ARCHIVOS", DUSK);
  canvas.setTextDatum(top_left); canvas.setTextSize(1);
  NetInfo info = getNetInfo();
  if (fsRunning) {
    canvas.setTextColor(SAGE);
    canvas.drawString("ACTIVO en http://" + info.localIp + ":8080", 6, 22);
    canvas.setTextColor(INK_DIM); canvas.drawString("comparte: " + fs::absolute(sdRoot()).string(), 6, 36);
    canvas.setTextColor(INK); canvas.drawString("Accesos recientes:", 6, 54);
    std::lock_guard<std::mutex> l(fsLogMtx);
    int y = 66;
    int start = (int)fsAccessLog.size() > 5 ? (int)fsAccessLog.size() - 5 : 0;
    for (int i = start; i < (int)fsAccessLog.size(); i++) { canvas.setTextColor(INK_DIM); canvas.drawString(fsAccessLog[i], 6, y); y += 11; }
  } else {
    canvas.setTextColor(INK_DIM);
    canvas.drawString("Detenido. ENTER para iniciar.", 6, 24);
    canvas.drawString("Nota: solo modo 'en mi red' (STA).", 6, 40);
    canvas.drawString("Crear un punto de acceso (AP) propio", 6, 54);
    canvas.drawString("no esta disponible en esta build.", 6, 66);
  }
  drawFooter("ENTER iniciar/detener   ` volver");
}
#endif

// ============================================================
// Manejo de teclado: imita los controles del Cardputer
// ( ; . mover,  ENTER elegir,  ` volver,  i info,  M mute )
// pero tambien acepta flechas/Backspace/Esc para comodidad en PC.
// ============================================================
void openCat(int i) { curCat = i; subSel = 0; screen = SUBMENU; beep(700, 40); }
void leaveToLauncher() { screen = LAUNCHER; beep(400, 30); }

#ifdef _WIN32
void resetToolState(Screen s) {
  switch (s) {
    case WIFI_SCAN: wifiScanStart(); break;
    case PING_SCR: pingLog.clear(); break;
    case PORTSCAN: portLog.clear(); break;
    case TRACE_SCR: traceLog.clear(); break;
    case HTTP_SCR: httpLog.clear(); break;
    case DNS_SCR: dnsLog.clear(); break;
    case DEV_COUNT: devLog.clear(); break;
    case FILE_EXP: curDir = sdRoot(); refreshDir(); break;
    default: break;
  }
}
#endif

void enterItem(Screen s) {
  screen = s; formStep = 0; beep(700, 50);
#ifdef _WIN32
  resetToolState(s);
#endif
}

void handleEnterOnScreen() {
#ifdef _WIN32
  switch (screen) {
    case WIFI_SCAN: wifiScanStart(); break;
    case PING_SCR: pingStart(hostBuf, atoi(portBuf.c_str())); break;
    case PORTSCAN: portScanStart(hostBuf); break;
    case TRACE_SCR: traceStart(hostBuf); break;
    case HTTP_SCR: httpStart(urlBuf); break;
    case DNS_SCR: dnsStart(hostBuf); break;
    case DEV_COUNT: devScanStart(hostBuf); break;
    case WOL_SCR: toast(wolSend(macBuf), SAGE); break;
    case PASSGEN: toast("nueva clave generada", SAGE); break;
    case HID_COMBO: sendCombo(COMBOS[comboSel]); toast("combo enviado", PLUM); break;
    case WIFI_SAVED: {
      auto profiles = wifiSavedProfiles();
      if (subSel < (int)profiles.size()) { wifiForget(profiles[subSel]); toast("perfil olvidado", RUST); }
      break;
    }
    case FILESRV: { if (fsRunning) fsStop(); else fsStart(); break; }
    case FILE_EXP: {
      if (fileSel < (int)dirEntries.size()) {
        auto &e = dirEntries[fileSel];
        if (e.is_directory()) { curDir = e.path().string(); refreshDir(); }
        else {
          fileViewPath = e.path().string();
          std::ifstream f(e.path(), std::ios::binary);
          std::ostringstream ss; ss << f.rdbuf();
          fileViewContent = ss.str();
          if (fileViewContent.size() > 2000) fileViewContent = fileViewContent.substr(0, 2000) + "\n...(recortado)";
          screen = FILE_VIEW;
        }
      }
      break;
    }
    default: break;
  }
#endif
}

void handleKeys(SDL_Event &ev) {
  if (ev.type != SDL_KEYDOWN) return;
  SDL_Keycode k = ev.key.keysym.sym;
  SDL_Keymod mod = SDL_GetModState();

  // --- entrada de texto activa: captura todo ---
  if (tin.active) {
    if (k == SDLK_BACKQUOTE || k == SDLK_ESCAPE) { tin.active = false; return; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      if (tin.owner == PING_SCR) { if (formStep == 0) { hostBuf = tin.buf; formStep = 1; openTextInput("Puerto", portBuf, PING_SCR); return; } else portBuf = tin.buf; }
      else if (tin.owner == PORTSCAN) hostBuf = tin.buf;
      else if (tin.owner == TRACE_SCR) hostBuf = tin.buf;
      else if (tin.owner == HTTP_SCR) urlBuf = tin.buf;
      else if (tin.owner == DNS_SCR) hostBuf = tin.buf;
      else if (tin.owner == DEV_COUNT) hostBuf = tin.buf;
      else if (tin.owner == WOL_SCR) macBuf = tin.buf;
      else if (tin.owner == SUBNET) { if (formStep == 0) { subnetIpBuf = tin.buf; formStep = 1; openTextInput("Prefijo (0-32)", subnetPrefixBuf, SUBNET); return; } else subnetPrefixBuf = tin.buf; }
      else if (tin.owner == RANGE_SCR) {
        if (formStep == 0) { rangeIpBuf = tin.buf; formStep = 1; openTextInput("Red", rangeNetBuf, RANGE_SCR); return; }
        else if (formStep == 1) { rangeNetBuf = tin.buf; formStep = 2; openTextInput("Prefijo", rangePrefixBuf, RANGE_SCR); return; }
        else rangePrefixBuf = tin.buf;
      }
      else if (tin.owner == IPCONV) ipConvBuf = tin.buf;
      else if (tin.owner == PASSGEN) passLenBuf = tin.buf;
      tin.active = false;
      return;
    }
    if (k == SDLK_BACKSPACE) { if (!tin.buf.empty()) tin.buf.pop_back(); return; }
    const char *txt = ev.key.keysym.sym < 128 ? SDL_GetKeyName(k) : "";
    return; // el texto real se captura en SDL_TEXTINPUT (ver main loop)
  }

  bool up = (k == SDLK_SEMICOLON || k == SDLK_UP);
  bool down = (k == SDLK_PERIOD || k == SDLK_DOWN);
  bool back = (k == SDLK_BACKQUOTE || k == SDLK_ESCAPE);
  bool info = (k == SDLK_i);
  bool enter = (k == SDLK_RETURN || k == SDLK_KP_ENTER);
  bool toggleMute = (k == SDLK_m);

  if (screen == SPLASH) { screen = WELCOME; return; }
  if (screen == WELCOME) { screen = LAUNCHER; beep(700, 60); return; }
  if (toggleMute) { muted = !muted; return; }

  if (screen == INSTR) { if (back || info) screen = instrFrom; return; }
  if (info) { instrFrom = screen; screen = INSTR; beep(600, 40); return; }

  switch (screen) {
    case LAUNCHER:
      if (up) { launchSel = (launchSel + NCAT - 1) % NCAT; beep(520, 20); }
      if (down) { launchSel = (launchSel + 1) % NCAT; beep(480, 20); }
      if (enter) openCat(launchSel);
      break;
    case SUBMENU: {
      int cnt = CATS[curCat].count;
      if (up) { subSel = (subSel + cnt - 1) % cnt; beep(520, 20); }
      if (down) { subSel = (subSel + 1) % cnt; beep(480, 20); }
      if (back) leaveToLauncher();
      if (enter) enterItem(TOOLS[CATS[curCat].first + subSel].scr);
      break;
    }
#ifdef _WIN32
    case WIFI_SCAN: if (k == SDLK_r) wifiScanStart(); if (back) leaveToLauncher(); break;
    case WIFI_TWIN: case NET_INFO: case CHAN_VIEW: case SIG_LIVE: case PKT_UNAVAIL:
    case HID_UNAVAIL: case IPCONV: case SD_INFO: case FILE_VIEW: case SUBNET: case RANGE_SCR:
      if (back) { if (screen == FILE_VIEW) screen = FILE_EXP; else leaveToLauncher(); }
      if (screen == SUBNET && k == SDLK_i) openTextInput("IP", subnetIpBuf, SUBNET);
      if (screen == SUBNET && k == SDLK_p) { formStep = 1; openTextInput("Prefijo", subnetPrefixBuf, SUBNET); }
      if (screen == RANGE_SCR && k == SDLK_i) { formStep = 0; openTextInput("IP", rangeIpBuf, RANGE_SCR); }
      if (screen == RANGE_SCR && k == SDLK_n) { formStep = 1; openTextInput("Red", rangeNetBuf, RANGE_SCR); }
      if (screen == RANGE_SCR && k == SDLK_p) { formStep = 2; openTextInput("Prefijo", rangePrefixBuf, RANGE_SCR); }
      if (screen == IPCONV && k == SDLK_t) openTextInput("IP", ipConvBuf, IPCONV);
      break;
    case WIFI_SAVED:
      if (up) subSel = std::max(0, subSel - 1);
      if (down) subSel++;
      if (enter) handleEnterOnScreen();
      if (back) leaveToLauncher();
      break;
    case PING_SCR: if (k == SDLK_t) { formStep = 0; openTextInput("Host", hostBuf, PING_SCR); } if (back) leaveToLauncher(); break;
    case PORTSCAN: if (k == SDLK_t) openTextInput("Host", hostBuf, PORTSCAN); if (back) leaveToLauncher(); break;
    case TRACE_SCR: if (k == SDLK_t) openTextInput("Host", hostBuf, TRACE_SCR); if (back) leaveToLauncher(); break;
    case HTTP_SCR: if (k == SDLK_t) openTextInput("URL", urlBuf, HTTP_SCR); if (back) leaveToLauncher(); break;
    case DNS_SCR: if (k == SDLK_t) openTextInput("Host", hostBuf, DNS_SCR); if (back) leaveToLauncher(); break;
    case DEV_COUNT: if (k == SDLK_t) openTextInput("Red (ej 192.168.1.1)", hostBuf, DEV_COUNT); if (back) leaveToLauncher(); break;
    case WOL_SCR:
      if (k == SDLK_t) openTextInput("MAC (AA:BB:CC:DD:EE:FF)", macBuf, WOL_SCR);
      if (enter) handleEnterOnScreen();
      if (back) leaveToLauncher();
      break;
    case PASSGEN:
      if (enter) handleEnterOnScreen();
      if (k == SDLK_u) passUpper = !passUpper;
      if (k == SDLK_n) passDigits = !passDigits;
      if (k == SDLK_s) passSymbols = !passSymbols;
      if (k == SDLK_l) openTextInput("Largo", passLenBuf, PASSGEN);
      if (back) leaveToLauncher();
      break;
    case HID_COMBO:
      if (up) comboSel = (comboSel + NCOMBO - 1) % NCOMBO;
      if (down) comboSel = (comboSel + 1) % NCOMBO;
      if (enter) handleEnterOnScreen();
      if (back) leaveToLauncher();
      break;
    case FILE_EXP:
      if (up) fileSel = std::max(0, fileSel - 1);
      if (down) fileSel = std::min((int)dirEntries.size() - 1, fileSel + 1);
      if (enter) handleEnterOnScreen();
      if (back) {
        fs::path p(curDir);
        if (fs::absolute(p) != fs::absolute(sdRoot()) && p.has_parent_path()) { curDir = p.parent_path().string(); refreshDir(); }
        else leaveToLauncher();
      }
      break;
    case FILESRV:
      if (enter) handleEnterOnScreen();
      if (back) leaveToLauncher();
      break;
#endif
    default: if (back) leaveToLauncher(); break;
  }
}

// ============================================================
// Dispatcher de dibujo
// ============================================================
void drawCurrentScreen() {
  canvas.fillScreen(BG0);
  switch (screen) {
    case SPLASH: drawSplash(); return;
    case WELCOME: drawWelcome(); return;
    case LAUNCHER: drawList(false); return;
    case SUBMENU: drawList(true); return;
    case INSTR: drawInstr(); return;
#ifdef _WIN32
    case WIFI_SCAN: drawWifiScan(); break;
    case NET_INFO: drawNetInfo(); break;
    case WIFI_SAVED: drawWifiSaved(); break;
    case WIFI_TWIN: drawWifiTwin(); break;
    case PING_SCR: drawPing(); break;
    case PORTSCAN: drawPortScan(); break;
    case TRACE_SCR: drawTrace(); break;
    case HTTP_SCR: drawHttp(); break;
    case DNS_SCR: drawDns(); break;
    case CHAN_VIEW: drawChanView(); break;
    case DEV_COUNT: drawDevCount(); break;
    case SIG_LIVE: drawSigLive(); break;
    case PKT_UNAVAIL: drawPktUnavail(); break;
    case SUBNET: drawSubnet(); break;
    case WOL_SCR: drawWol(); break;
    case PASSGEN: drawPassGen(); break;
    case RANGE_SCR: drawRange(); break;
    case IPCONV: drawIpConv(); break;
    case HID_COMBO: drawHidCombo(); break;
    case HID_UNAVAIL: drawHidUnavail(); break;
    case FILE_EXP: drawFileExp(); break;
    case FILE_VIEW: drawFileView(); break;
    case SD_INFO: drawSdInfo(); break;
    case FILESRV: drawFileSrv(); break;
#endif
    default: break;
  }
  drawToast();
  drawTextInputOverlay();
}

// ============================================================
// main()
// ============================================================
int main(int argc, char **argv) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }
  if (TTF_Init() != 0) {
    fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
    return 1;
  }
  SDL_Window *win = SDL_CreateWindow("Seven Tools of the Bandit - PC Edition",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
  SDL_Renderer *screenRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  SDL_RenderSetLogicalSize(screenRen, WIN_W, WIN_H);
  canvas.init(screenRen);

  SDL_AudioSpec want{}, have{};
  want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
  want.callback = audioCallback;
  audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (audioDev) SDL_PauseAudioDevice(audioDev, 0);

#ifdef _WIN32
  ensureWinsock();
  curDir = sdRoot();
  refreshDir();
#endif

  splashStart = nowMs();
  bool running = true;
  SDL_StartTextInput();

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN) handleKeys(ev);
      else if (ev.type == SDL_TEXTINPUT && tin.active) {
        tin.buf += ev.text.text;
      }
    }
    if (screen == SPLASH && nowMs() - splashStart > SPLASH_MS) screen = WELCOME;

    canvas.begin();
    drawCurrentScreen();
    canvas.present(screenRen);

    SDL_SetRenderTarget(screenRen, nullptr);
    SDL_RenderClear(screenRen);
    SDL_Rect dst{0, 0, WIN_W, WIN_H};
    SDL_RenderCopy(screenRen, canvas.target, nullptr, &dst);
    SDL_RenderPresent(screenRen);

    SDL_Delay(16);
  }

#ifdef _WIN32
  if (fsRunning) fsStop();
#endif
  SDL_StopTextInput();
  if (audioDev) SDL_CloseAudioDevice(audioDev);
  TTF_Quit();
  SDL_DestroyRenderer(screenRen);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
