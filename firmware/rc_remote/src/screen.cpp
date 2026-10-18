#include "screen.h"

#include <LovyanGFX.hpp>
#include <U8g2lib.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

namespace {

class Tft : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  Tft() {
    {
      auto c = bus_.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.pin_sclk = TFT_SCLK;
      c.pin_mosi = TFT_MOSI;
      c.pin_miso = -1;
      c.pin_dc = TFT_DC;
      c.dma_channel = SPI_DMA_CH_AUTO;
      bus_.config(c);
      panel_.setBus(&bus_);
    }
    {
      auto c = panel_.config();
      c.pin_cs = TFT_CS;
      c.pin_rst = TFT_RST;
      c.panel_width = 240;
      c.panel_height = 280;
      c.memory_width = 240;
      c.memory_height = 320;
      c.offset_y = TFT_OFFSET_Y;
      c.invert = true;          // IPS ST7789 paneļiem krāsas ir apgrieztas
      c.readable = false;
      panel_.config(c);
    }
    {
      auto c = light_.config();
      c.pin_bl = TFT_BL;
      c.freq = 20000;
      c.pwm_channel = 7;        // 0-2 LED, 4 pīkstulis
      light_.config(c);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

Tft tft;
LGFX_Sprite spr(&tft);   // viss kadrs atmiņā -> bez mirgošanas

// Fonti ar latviešu burtiem (U8g2 "_te" = ar Latin Extended)
const lgfx::U8g2font fSmall(u8g2_font_helvR10_te);
const lgfx::U8g2font fMid(u8g2_font_helvB12_te);
const lgfx::U8g2font fBig(u8g2_font_helvB18_te);

int W = 240, H = 280;   // pēc pagriešanas: tft.width() / tft.height()
const uint32_t cBg = 0x15181D, cPanel = 0x262C35, cLine = 0x333B46, cText = 0xE6E9EE, cDim = 0x8B95A3,
               cAccent = 0xE0573A, cOk = 0x3FB67A, cWarn = 0xE3A72F, cBad = 0xE5484D, cBlue = 0x4C8DFF;

uint32_t c8(uint32_t rgb) { return lgfx::color888(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); }

void text(const lgfx::IFont &f, uint32_t col, int x, int y, const char *s, uint8_t datum = top_left) {
  spr.setFont(&f);
  spr.setTextColor(c8(col));
  spr.setTextDatum(datum);
  spr.drawString(s, x, y);
}

// Josla: -180..180° (vai 0..100 % satvērējam), aizpildījums no nulles, marķieris
void bar(int x, int y, int w, int h, float v, float lo, float hi, uint32_t col) {
  spr.fillRoundRect(x, y, w, h, 4, c8(cPanel));
  auto px = [&](float a) { return x + (int)((constrain(a, lo, hi) - lo) / (hi - lo) * (w - 1)); };
  const int x0 = px(lo < 0 && hi > 0 ? 0 : lo), xp = px(v);
  spr.fillRect(min(x0, xp), y + 2, abs(xp - x0) + 1, h - 4, c8((col & 0xFEFEFE) >> 1));   // tumšāka
  if (lo < 0) spr.drawFastVLine(x0, y, h, c8(cDim));
  spr.fillRoundRect(xp - 2, y + 1, 5, h - 2, 2, c8(col));
}

const char *stateName(uint8_t s) {
  switch (s) {
    case 1: return "gatavs";
    case 2: return "manuāli";
    case 3: return "kustība";
    case 4: return "AVĀRIJA";
    case 5: return "HOME";
    default: return "kontrolieris klusē";
  }
}

}  // namespace

namespace screen {

void begin() {
  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setBrightness(TFT_BRIGHTNESS);
  W = tft.width();
  H = tft.height();
  spr.setColorDepth(8);           // 240x280x1 = 67 KB (iekšējā RAM)
  spr.createSprite(W, H);
  spr.fillScreen(c8(cBg));
  text(fBig, cText, W / 2, H / 2, "Robot roka", middle_center);
  spr.pushSprite(0, 0);
}

void reinit() {
  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setBrightness(TFT_BRIGHTNESS);
}

void draw(const ScreenData &d) {
  char b[48];
  spr.fillScreen(c8(cBg));

  if (d.ota >= 0) {
    text(fBig, cWarn, W / 2, 6, "ATJAUNINĀŠANA", top_center);
    if (d.otaIp) {
      text(fMid, cOk, 8, 40, "mājas tīklā:");
      text(fBig, cText, 8, 60, d.otaIp);
    } else {
      text(fMid, cDim, 8, 40, "mājas tīkls: nav pieslēgts");
    }
    text(fSmall, cText, 8, 92, "Wi-Fi: " OTA_AP_SSID "  parole: " OTA_AP_PASS);
    text(fSmall, cBlue, 8, 110, "http://192.168.4.1  (iestatījumi)");
    text(fSmall, cDim, 8, 128, "pio run -e s3_ota -t upload --upload-port IP");
    if (d.ota > 0) {
      snprintf(b, sizeof b, "ielādē... %d %%", d.ota);
      text(fMid, cOk, W / 2, 160, b, top_center);
      bar(20, 190, W - 40, 18, d.ota, 0, 100, cOk);
    } else {
      text(fSmall, cDim, W / 2, 170, "izslēdz pulti, lai izietu", top_center);
    }
    spr.pushSprite(0, 0);
    return;
  }

  if (d.swWarn) {
    static const char *kSwLabels[5] = SW_LABELS;
    static const char *kSwFn[5] = {"ARM", "avārija", "J4/J5", "", ""};
    text(fBig, cBad, W / 2, 6, "PĀRSLĒDZ SLĒDŽUS", top_center);
    text(fSmall, cDim, W / 2, 32, "pults gaida, kamēr visi ir sākuma pozīcijā", top_center);
    int y = 56;
    for (uint8_t i = 0; i < 5; i++) {
      if (!(d.swWarn & (1u << i))) continue;
      snprintf(b, sizeof b, "%s %s: izslēgt", kSwLabels[i], kSwFn[i]);
      text(fMid, cWarn, 16, y, b);
      y += 24;
    }
    if (d.swWarn & 0x20) { text(fMid, cWarn, 16, y, "E ātrums: lēni"); y += 24; }
    if (d.swWarn & 0x40) { text(fMid, cWarn, 16, y, "C režīms: vadība"); y += 24; }
    spr.pushSprite(0, 0);
    return;
  }

  if (d.calStep) {
    text(fBig, cWarn, W / 2, H / 8, "KALIBRĀCIJA", top_center);
    text(fMid, cText, W / 2, H * 3 / 8, d.calStep == 1 ? "1) atlaid stikus centrā" : "2) visus stikus un pogu", top_center);
    if (d.calStep == 2) text(fMid, cText, W / 2, H * 3 / 8 + 22, "līdz galiem", top_center);
    text(fMid, cDim, W / 2, H * 5 / 8, "tad īsi nospied pogu", top_center);
    text(fSmall, cDim, W / 2, H * 5 / 8 + 22, "(nefiksējošo vai BOOT)", top_center);
    spr.pushSprite(0, 0);
    return;
  }

  // --- augšējā josla: saite, ARM, baterija
  spr.fillRect(0, 0, W, 28, c8(cPanel));
  text(fMid, d.link ? cOk : cBad, 6, 7, d.link ? "SAITE" : "NAV SAITES");
  if (d.armed) {
    spr.fillRoundRect(W / 2 - 20, 4, 48, 20, 5, c8(cBlue));
    text(fMid, 0xFFFFFF, W / 2 + 4, 14, "ARM", middle_center);
  }
  // slēdži A, B, D (SW3..SW5): mazi lodziņi (iedegas, kad ieslēgts)
  for (uint8_t i = 0; i < 3; i++) {
    const bool on = d.switches & (1u << (2 + i));
    const int x = W / 2 + 34 + i * 19;
    if (on) spr.fillRoundRect(x, 6, 16, 16, 3, c8(cOk));
    else spr.drawRoundRect(x, 6, 16, 16, 3, c8(cDim));
    static const char *kSwLabels[5] = SW_LABELS;
    snprintf(b, sizeof b, "%s", kSwLabels[2 + i]);
    text(fSmall, on ? 0x000000 : cDim, x + 8, 14, b, middle_center);
  }
  if (d.charge) {
    snprintf(b, sizeof b, "%s %u.%u V", d.charge == 2 ? "pilna" : "lādējas", (d.battMv + 50) / 1000, ((d.battMv + 50) % 1000) / 100);
    text(fSmall, d.charge == 2 ? cOk : cWarn, W - 6, 9, b, top_right);
  } else {
    snprintf(b, sizeof b, "%u.%u V", (d.battMv + 50) / 1000, ((d.battMv + 50) % 1000) / 100);
    text(fMid, d.battLow ? cBad : cText, W - 6, 7, b, top_right);
  }

  // --- ātrums, režīms; pa labi stāvoklis vai nesen nospiestā poga
  static const char *kSpeed[3] = {"lēni", "vidēji", "ātri"};
  static const char *kMode[3] = {"vadība", "mācīšana", "atskaņošana"};
  snprintf(b, sizeof b, "%s · %s", kSpeed[d.speedPos < 3 ? d.speedPos : 1], kMode[d.mode < 3 ? d.mode : 0]);
  text(fSmall, d.mode == 1 ? 0xD070FF : cDim, 6, 33, b);
  if (d.pressed >= 0) {
    if (d.pressed == RC_BTN_MOMENT) snprintf(b, sizeof b, "poga H");
    else snprintf(b, sizeof b, "poga P%d", d.pressed);
    text(fMid, cWarn, W - 6, 31, b, top_right);
  } else if (d.estop) {
    text(fMid, cBad, W - 6, 31, "AVĀRIJA", top_right);
  } else if (d.link) {
    text(fSmall, d.tel.ctrlState == 4 ? cBad : cDim, W - 6, 33, stateName(d.tel.ctrlState), top_right);
  }

  if (!d.link) {
    text(fBig, cWarn, W / 2, H / 2 - 16, "meklē roku...", top_center);
    snprintf(b, sizeof b, "Wi-Fi kanāls %u", d.channel);
    text(fMid, cDim, W / 2, H / 2 + 16, b, top_center);
  } else {
    // --- 5 locītavas + satvērējs: nosaukums, leņķis, josla (rindu augstums pēc ekrāna)
    const int top = 50, rowH = (H - top - 2) / 6, barX = 124, barW = W - barX - 6, barH = rowH > 32 ? 18 : 14;
    for (uint8_t r = 0; r < 6; r++) {
      const int y = top + r * rowH;
      const int yb = y + (rowH - barH) / 2;
      if (r < RC_NUM_JOINTS) {
        const bool z = d.tel.zeroMask & (1u << r), es = d.tel.esMask & (1u << r);
        const float deg = d.tel.pos[r] / 10.0f;
        // kreisais stiks ↔ vada J4 vai J5 (SW3) - to iezīmējam zilu
        const bool leftX = (r == 3 && !(d.switches & 4)) || (r == 4 && (d.switches & 4));
        snprintf(b, sizeof b, z ? "J%u" : "J%u?", r + 1);
        text(fMid, !z ? cWarn : (leftX ? cBlue : cDim), 6, yb + 2, b);
        snprintf(b, sizeof b, "%.1f°", deg);
        text(fBig, z ? cText : cDim, barX - 6, yb - 1, b, top_right);
        bar(barX, yb, barW, barH, deg, -180, 180, es ? cBad : (z ? cAccent : cDim));
      } else {
        text(fMid, cDim, 6, yb + 2, "G");
        snprintf(b, sizeof b, "%u %%", d.tel.grip);
        text(fBig, cText, barX - 6, yb - 1, b, top_right);
        bar(barX, yb, barW, barH, d.tel.grip, 0, 100, cBlue);
      }
    }
  }
  spr.pushSprite(0, 0);
}

}  // namespace screen
