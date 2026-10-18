#pragma once
// =============================================================================
//  Plašu pinu definīcijas.  Asu sadalījums:  J1=X  J2=Y  J3=Z  J4=E0  J5=E1
//  Ekrāna pini - kā Marlin "REPRAP_DISCOUNT_SMART_CONTROLLER".
// =============================================================================
#include "config.h"

#if defined(BOARD_RAMPS14)
  // Arduino Mega 2560 + RAMPS 1.4
  #define BOARD_NAME "RAMPS 1.4"

  #define STEP_PINS   { 54, 60, 46, 26, 36 }
  #define DIR_PINS    { 55, 61, 48, 28, 34 }
  #define ENABLE_PINS { 38, 56, 62, 24, 30 }
  #define ENABLE_ACTIVE_LOW 1

  // Gala slēdži: X-MIN, Y-MIN, AUX-1 (A3), X-MAX, Y-MAX.
  // Z-MIN/Z-MAX (D18/D19) aizņem ESP32 (Serial1), jo D16/D17 vajag ekrānam.
  #define ENDSTOP_PINS { 3, 14, 57, 2, 15 }

  #define GRIPPER_SERVO_PIN 11     // SERVOS galviņa, pirmais pins
  #define STATUS_LED_PIN    13

  #define USB_PORT Serial
  #define ESP_PORT Serial1         // Z-MIN "S" = D18 = TX1, Z-MAX "S" = D19 = RX1 (5V loģika!)

  // Ekrāns (Smart Adapter): AUX-4 + AUX-3
  #define LCD_RS_PIN   16
  #define LCD_EN_PIN   17
  #define LCD_D4_PIN   23
  #define LCD_D5_PIN   25
  #define LCD_D6_PIN   27
  #define LCD_D7_PIN   29
  #define BTN_EN1_PIN  31
  #define BTN_EN2_PIN  33
  #define BTN_ENC_PIN  35
  #define BEEPER_PIN   37
  #define KILL_PIN     41          // ekrāna "STOP/X" poga
  #define SD_DETECT_PIN 49
  #define SD_SS_PIN    53          // SPI: 50 MISO, 51 MOSI, 52 SCK

  // AVR ir lēns: 16 kHz soļu ISR -> līdz 8000 soļi/s uz asi.
  #define STEP_ISR_HZ 16000UL
  #define CONTROL_HZ  200UL
  #define MOVE_QUEUE_SIZE 8

#elif defined(BOARD_SKR14_TURBO)
  // BTT SKR V1.4 Turbo (LPC1769, 120 MHz) + 5x TMC2209 V1.2 UART režīmā
  #define BOARD_NAME "BTT SKR V1.4 Turbo"

  #define STEP_PINS     { P2_02, P0_19, P0_22, P2_13, P1_15 }
  #define DIR_PINS      { P2_06, P0_20, P2_11, P0_11, P1_14 }
  #define ENABLE_PINS   { P2_01, P2_08, P0_21, P2_12, P1_16 }
  #define ENABLE_ACTIVE_LOW 1
  #define TMC_UART_PINS { P1_10, P1_09, P1_08, P1_04, P1_01 }

  // X-STOP, Y-STOP, Z-STOP, E0DET, E1DET
  #define ENDSTOP_PINS { P1_29, P1_28, P1_27, P1_26, P1_25 }

  #define GRIPPER_SERVO_PIN P2_00  // "SERVOS" galviņa (PWM1.1)
  // Statusa LED nav: P1_18 ir ekrāna EN pins.

  #define USB_PORT UsbSerial
  #define ESP_PORT Serial          // TFT galviņa: P0_02 = TX0, P0_03 = RX0 (3.3V)

  // Ekrāns: EXP1 / EXP2.  EXP2 8. kontakts = plates RESET (ekrāna "X" poga pārstartē plati)
  #define LCD_RS_PIN   P1_19
  #define LCD_EN_PIN   P1_18
  #define LCD_D4_PIN   P1_20
  #define LCD_D5_PIN   P1_21
  #define LCD_D6_PIN   P1_22
  #define LCD_D7_PIN   P1_23
  #define BTN_EN1_PIN  P3_26
  #define BTN_EN2_PIN  P3_25
  #define BTN_ENC_PIN  P0_28
  #define BEEPER_PIN   P1_30
  #define SD_DETECT_PIN P1_31

  #define STEP_ISR_HZ 50000UL      // līdz 25000 soļi/s uz asi
  #define CONTROL_HZ  1000UL
  #define MOVE_QUEUE_SIZE 32

#else
  #error "Nav izvēlēta plate: izmanto pio run -e ramps14 vai -e skr14turbo"
#endif
