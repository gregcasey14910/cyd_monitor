/*
 * CYD Relay Computer Monitor - PORTRAIT MODE
 * 240x320 - Casey RELAY Comp
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// MCP23017 I2C - CYD CN1: SDA=GPIO27, SCL=GPIO22
#define MCP_SDA       27
#define MCP_SCL       22
#define MCP_ADDR      0x20
// MCP23017 registers (BANK=0 default)
#define MCP_IODIRA    0x00
#define MCP_IODIRB    0x01
#define MCP_GPPUA     0x0C
#define MCP_GPPUB     0x0D
#define MCP_GPIOA     0x12
#define MCP_GPIOB     0x13
#define MCP_OLATA     0x14

bool     mcp_found    = false;
uint8_t  mcp_led_state  = 0x00;  // current LED output state (Port A)
uint8_t  mcp_btn_state  = 0xFF;  // current button state (Port B, active LOW)
String   lastSerialCmd  = "none";

// CYD Pin definitions for HARDWARE SPI
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   21

// RGB LED pins on CYD (active LOW!)
#define LED_RED   4
#define LED_GREEN 16
#define LED_BLUE  17

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

String macAddress = "";

// Screen type selection
const int screen_type = 3;  // 1 = scrolling, 2 = ALU graphic, 3 = MCP23017 debug

// ESP-NOW message structure
typedef struct struct_message {
  int cmd;
  int bus_value;
} struct_message;

// Command enum
enum RemoteCmd {
  PING        = 0x0,   // Ping/heartbeat
  SEL_REGA    = 0x1,   // Select Register A
  SEL_REGD    = 0x2,   // Select Register D
  SEL_AD      = 0x3,   // Select both A and D
  CLK_REGA    = 0x11,  // Clock Register A
  CLK_REGD    = 0x12,  // Clock Register D
  CLK_AD      = 0x13,  // Clock both A and D

  SEL_REGB    = 0x4,   // Select Register B
  SEL_REGC    = 0x5,   // Select Register C
  SEL_BC      = 0x6,   // Select both B and C
  CLK_REGB    = 0x14,  // Clock Register B
  CLK_REGC    = 0x15,  // Clock Register C
  CLK_BC      = 0x16,  // Clock both B and C

  SEL_REGM1   = 0x7,   // Select Register M1
  SEL_REGM2   = 0x8,   // Select Register M2
  SEL_M       = 0x9,   // Select both M1 and M2
  CLK_REGM1   = 0x17,  // Clock Register M1
  CLK_REGM2   = 0x18,  // Clock Register M2
  CLK_M1M2    = 0x19,  // Clock both M1 and M2

  SEL_REGX    = 0xa,   // Select Register X
  SEL_REGY    = 0xb,   // Select Register Y
  SEL_XY      = 0xc,   // Select both X and Y
  CLK_REGX    = 0x1a,  // Clock Register X
  CLK_REGY    = 0x1b,  // Clock Register Y
  CLK_XY      = 0x1c,  // Clock both X and Y

  SEL_REGJ1   = 0xd,   // Select Register J1
  SEL_REGJ2   = 0xe,   // Select Register J2
  SEL_J       = 0xf,   // Select both J1 and J2
  CLK_REGJ1   = 0x1d,  // Clock Register J1
  CLK_REGJ2   = 0x1e,  // Clock Register J2
  CLK_J       = 0x1f,  // Clock both J1 and J2
  
  FCT_ALU     = 0x20,  // Function code to ALU
  B_2_ALU     = 0x21,  // SEND BReg to ALU
  C_2_ALU     = 0x22,  // Send CReg to ALU
  BC_2_ALU    = 0x23,  // Send BC to ALU
  ALU_rsv1    = 0x24,
  ALU_COMBO   = 0x30,  // ALU output/result
  ALU_2_DBUS  = 0x31,  // Gate ALU to DBUS
  CLK_SR      = 0x32,  // Gate SR 
  HCT_MS      = 0x33,  // implied codes for the 74HCT181 ALU
  ALU_SCZ     = 0x34   // Sign, Carry, Zero status bits
};

// ALU display state (for screen_type = 2)
uint8_t alu_b_value = 0;
uint8_t alu_c_value = 0;
uint8_t alu_f_code = 0;
uint8_t alu_output = 0;  // ALU result/output
uint8_t hct_ms = 0;      // 74HC181 M and S control bits
uint8_t alu_scz = 0;     // Sign, Carry, Zero status bits

// Message queue (for screen_type = 1)
const int MAX_MESSAGES = 100;
String messageLog[MAX_MESSAGES];
int messageCount = 0;

// Flag for new messages
volatile bool newMessageFlag = false;
String pendingMessage = "";

const int lineHeight = 18;
const int displayStartY = 50;
const int displayHeight = 320 - displayStartY;
const int maxVisibleLines = displayHeight / lineHeight;

// Convert command code to name
String getCommandName(int cmd) {
  switch(cmd) {
    case PING:        return "PING";
    case SEL_REGA:    return "SEL_REGA";
    case SEL_REGD:    return "SEL_REGD";
    case SEL_AD:      return "SEL_AD";
    case SEL_REGB:    return "SEL_REGB";
    case SEL_REGC:    return "SEL_REGC";
    case SEL_BC:      return "SEL_BC";
    case SEL_REGM1:   return "SEL_M1";
    case SEL_REGM2:   return "SEL_M2";
    case SEL_M:       return "SEL_M";
    case SEL_REGX:    return "SEL_X";
    case SEL_REGY:    return "SEL_Y";
    case SEL_XY:      return "SEL_XY";
    case SEL_REGJ1:   return "SEL_J1";
    case SEL_REGJ2:   return "SEL_J2";
    case SEL_J:       return "SEL_J";
    
    case CLK_REGA:    return "CLK_REGA";
    case CLK_REGD:    return "CLK_REGD";
    case CLK_AD:      return "CLK_AD";
    case CLK_REGB:    return "CLK_REGB";
    case CLK_REGC:    return "CLK_REGC";
    case CLK_BC:      return "CLK_BC";
    case CLK_REGM1:   return "CLK_M1";
    case CLK_REGM2:   return "CLK_M2";
    case CLK_M1M2:    return "CLK_M";
    case CLK_REGX:    return "CLK_X";
    case CLK_REGY:    return "CLK_Y";
    case CLK_XY:      return "CLK_XY";
    case CLK_REGJ1:   return "CLK_J1";
    case CLK_REGJ2:   return "CLK_J2";
    case CLK_J:       return "CLK_J";
    
    case FCT_ALU:     return "F__ALU";
    case B_2_ALU:     return "B->ALU";
    case C_2_ALU:     return "C->ALU";
    case BC_2_ALU:    return "BC->ALU";
    case ALU_rsv1:    return "ALU_r1";
    case ALU_COMBO:   return "ALU_OUT";
    case ALU_2_DBUS:  return "ALU->DB";
    case CLK_SR:      return "CLK_SR";
    case HCT_MS:      return "HCT_MS";
    case ALU_SCZ:     return "ALU_SCZ";
    
    default:
      char hexbuf[8];
      sprintf(hexbuf, "?%02X", cmd);
      return String(hexbuf);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n======================");
  Serial.println("CYD ESP-NOW Monitor");
  Serial.println("PORTRAIT MODE");
  Serial.println("======================");
  
  // Init RGB LED pins (active LOW - HIGH = OFF)
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);   // OFF
  digitalWrite(LED_GREEN, HIGH); // OFF
  digitalWrite(LED_BLUE, HIGH);  // OFF
  
  // Backlight ON
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  // Init Hardware SPI
  SPI.begin(14, 12, 13, 15);
  
  // Init display
  Serial.println("Init display...");
  tft.begin(40000000);
  tft.setRotation(2);  // 180 degrees - USB cable at top
  tft.fillScreen(ILI9341_BLACK);
  
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 150);
  tft.print("Starting...");
  delay(1000);
  
  // Get MAC
  Serial.println("Init WiFi...");
  WiFi.mode(WIFI_STA);
  delay(500);
  
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macAddress = String(macStr);
  
  Serial.print("MAC: ");
  Serial.println(macAddress);
  Serial.print("Max visible lines: ");
  Serial.println(maxVisibleLines);
  
  WiFi.disconnect();
  
  // Init ESP-NOW
  Serial.println("Init ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW FAILED!");
    tft.fillScreen(ILI9341_RED);
    tft.setCursor(10, 150);
    tft.print("ESP-NOW");
    tft.setCursor(10, 170);
    tft.print("FAILED!");
    while(1) delay(1000);
  }
  
  esp_now_register_recv_cb(onDataReceive);
  Serial.println("ESP-NOW Ready!");
  
  // Draw UI
  tft.fillScreen(ILI9341_BLACK);
  drawHeader();
  drawMacBanner();
  
  if (screen_type == 1) {
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(5, displayStartY);
    tft.print("Listening...");
  } else if (screen_type == 2) {
    drawALU();
  } else if (screen_type == 3) {
    initMCP();
    drawMCP();
    testLEDs();
  }

  Serial.println("Ready!\n");
  if (screen_type == 3) {
    Serial.println("MCP23017 Serial Commands:");
    Serial.println("  TEST   : Walk LED test");
    Serial.println("  L1-L8  : Toggle LED");
    Serial.println("  LA     : All LEDs ON");
    Serial.println("  LX     : All LEDs OFF");
    Serial.println("  B?     : Read buttons");
    Serial.println("  SCAN   : I2C scan");
  }
}

void loop() {
  // Check for new message
  if (newMessageFlag) {
    String msg = pendingMessage;
    newMessageFlag = false;
    
    // Flash RANDOM color on RGB LED
    // Active LOW - so random 0 or 1, then invert
    bool r = random(0, 2) == 0; // random ON/OFF
    bool g = random(0, 2) == 0;
    bool b = random(0, 2) == 0;
    
    // Write inverted (LOW = ON)
    digitalWrite(LED_RED, !r);
    digitalWrite(LED_GREEN, !g);
    digitalWrite(LED_BLUE, !b);
    
    if (screen_type == 1) {
      // Add to log for scrolling display
      if (messageCount < MAX_MESSAGES) {
        messageLog[messageCount] = msg;
        messageCount++;
      } else {
        // Shift up in buffer
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
          messageLog[i] = messageLog[i + 1];
        }
        messageLog[MAX_MESSAGES - 1] = msg;
      }
      
      Serial.print("RX: ");
      Serial.println(msg);
      
      // Redraw display
      redrawDisplay();
    } else if (screen_type == 2) {
      // ALU display mode - just redraw
      Serial.print("RX: ");
      Serial.println(msg);
      drawALU();
    }
  }
  
  // Serial command handler (screen_type=3)
  static String serialBuf = "";
  while (screen_type == 3 && Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() == 0) continue;
      String cmd = serialBuf;
      serialBuf = "";
      cmd.trim();
      cmd.toUpperCase();
      lastSerialCmd = cmd;

      if (cmd == "TEST") {
        lastSerialCmd = cmd;
        testLEDs();
        drawMCP();
      } else if (cmd == "SCAN") {
        Serial.println("Scanning I2C...");
        for (uint8_t addr = 1; addr < 127; addr++) {
          Wire.beginTransmission(addr);
          if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X\n", addr);
          }
        }
        Serial.println("Scan done.");
      } else if (cmd == "LA") {
        mcp_led_state = 0xFF;
        mcpWriteReg(MCP_OLATA, mcp_led_state);
        Serial.println("All LEDs ON");
        drawMCP();
      } else if (cmd == "LX") {
        mcp_led_state = 0x00;
        mcpWriteReg(MCP_OLATA, mcp_led_state);
        Serial.println("All LEDs OFF");
        drawMCP();
      } else if (cmd == "B?") {
        mcp_btn_state = mcp_found ? mcpReadReg(MCP_GPIOB) : mcp_led_state;
        Serial.printf("Button state: 0x%02X%s\n", mcp_btn_state, mcp_found ? "" : " (LED mirror)");
        for (int i = 0; i < 8; i++) {
          bool active = (mcp_btn_state >> i) & 0x01;
          Serial.printf("  SW%d: %s\n", i+1, active ? "ON" : "off");
        }
        drawMCP();
      } else if (cmd.startsWith("B") && cmd.length() == 2 && cmd.charAt(1) >= '1' && cmd.charAt(1) <= '8') {
        int n = cmd.charAt(1) - '1';  // 0-7
        mcp_btn_state = mcp_found ? mcpReadReg(MCP_GPIOB) : mcp_led_state;
        bool active = (mcp_btn_state >> n) & 0x01;
        Serial.printf("SW%d: %s\n", n+1, active ? "ON" : "off");
      } else if (cmd.startsWith("L") && cmd.length() == 2) {
        int n = cmd.charAt(1) - '1';  // 0-7
        if (n >= 0 && n <= 7) {
          mcp_led_state ^= (1 << n);  // toggle
          mcpWriteReg(MCP_OLATA, mcp_led_state);
          Serial.printf("LED%d toggled -> 0x%02X\n", n+1, mcp_led_state);
          drawMCP();
        }
      } else {
        Serial.printf("Unknown cmd: %s\n", cmd.c_str());
      }
    } else {
      serialBuf += c;
    }
  }

  // Button polling - every 100ms
  if (screen_type == 3 && mcp_found) {
    static unsigned long lastBtnPoll = 0;
    if (millis() - lastBtnPoll >= 100) {
      lastBtnPoll = millis();
      uint8_t newBtnState = mcpReadReg(MCP_GPIOB);
      if (newBtnState != mcp_btn_state) {
        mcp_btn_state = newBtnState;
        // Log any newly pressed buttons (active LOW: 0 = pressed)
        for (int i = 0; i < 8; i++) {
          bool nowPressed = !((newBtnState >> i) & 0x01);
          if (nowPressed) Serial.printf("BTN%d pressed\n", i + 1);
        }
        drawMCP();
      }
    }
  }

  // Uptime counter - update every second
  if (screen_type == 3) {
    static unsigned long lastUptimeDraw = 0;
    if (millis() - lastUptimeDraw >= 1000) {
      lastUptimeDraw = millis();
      drawUptime();
    }
  }

  delay(50);
}

void drawUptime() {
  unsigned long t = millis() / 1000;
  uint16_t hh = t / 3600;
  uint8_t  mm = (t % 3600) / 60;
  uint8_t  ss = t % 60;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);

  // Clear uptime row only
  tft.fillRect(0, 280, 240, 36, ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(48, 284);   // 8 chars * 18px = 144px wide, centered: (240-144)/2 = 48
  tft.print(buf);
}

// ESP-NOW callback
void onDataReceive(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  // Parse struct
  struct_message receivedData;
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  
  if (screen_type == 1) {
    // Scrolling mode - format as text
    String cmdName = getCommandName(receivedData.cmd);
    char buffer[40];
    sprintf(buffer, "%-9s:0x%02X", cmdName.c_str(), receivedData.bus_value);
    pendingMessage = String(buffer);
  } else if (screen_type == 2) {
    // ALU graphic mode - update state
    if (receivedData.cmd == FCT_ALU) {
      alu_f_code = receivedData.bus_value & 0x07;  // Lower 3 bits
    } else if (receivedData.cmd == B_2_ALU) {
      alu_b_value = receivedData.bus_value;
    } else if (receivedData.cmd == C_2_ALU) {
      alu_c_value = receivedData.bus_value;
    } else if (receivedData.cmd == ALU_COMBO) {
      alu_output = receivedData.bus_value;  // ALU result
    } else if (receivedData.cmd == HCT_MS) {
      hct_ms = receivedData.bus_value;  // Store M and S bits
    } else if (receivedData.cmd == ALU_SCZ) {
      alu_scz = receivedData.bus_value;  // Store SCZ status bits
    }
    pendingMessage = "ALU_UPDATE";
  }
  
  newMessageFlag = true;
}

void redrawDisplay() {
  // Clear message area
  tft.fillRect(0, displayStartY, 240, displayHeight, ILI9341_BLACK);
  
  // Figure out which messages to display (last N messages)
  int numToShow = min(messageCount, maxVisibleLines);
  int startIdx = messageCount - numToShow;
  
  // Draw messages from top to bottom
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  
  for (int i = 0; i < numToShow; i++) {
    int msgIdx = startIdx + i;
    int yPos = displayStartY + (i * lineHeight);
    
    tft.setCursor(5, yPos);
    tft.print(messageLog[msgIdx]);
  }
}

// Get ALU function name from F code
String getALUFunctionName(uint8_t fcode) {
  switch(fcode & 0x07) {  // Ensure only 3 bits
    case 0: return "CLR";
    case 1: return "ADD";
    case 2: return "INC";
    case 3: return "AND";
    case 4: return "OR";
    case 5: return "XOR";
    case 6: return "NOT";
    case 7: return "SHL";
    default: return "???";
  }
}

void drawALU() {
  // Clear working area (below banners)
  tft.fillRect(0, displayStartY, 240, displayHeight, ILI9341_BLACK);
  
  // ALU trapezoid coordinates - NARROWER, WIDE on LEFT (inputs), NARROW on RIGHT (output)
  int16_t x_left = 80;     // Left edge (wide side)
  int16_t x_right = 180;   // Right edge (narrow side)
  int16_t y_top = 120;     // Moved down more to make room for function line
  int16_t y_bottom = 260;
  int16_t y_center = (y_top + y_bottom) / 2;
  
  // Calculate narrow right side points
  int16_t y_right_top = y_center - 25;
  int16_t y_right_bottom = y_center + 25;
  
  // Calculate center point of trapezoid top edge for function leader
  int16_t trap_top_center_x = (x_left + x_right) / 2;
  int16_t trap_top_center_y = (y_top + y_right_top) / 2;
  
  // Draw ALU body (trapezoid - wide left, narrow right)
  tft.drawLine(x_left, y_top, x_right, y_right_top, ILI9341_GREEN);           // Top edge
  tft.drawLine(x_left, y_bottom, x_right, y_right_bottom, ILI9341_GREEN);     // Bottom edge
  tft.drawLine(x_left, y_top, x_left, y_bottom, ILI9341_GREEN);               // Left edge (wide)
  tft.drawLine(x_right, y_right_top, x_right, y_right_bottom, ILI9341_GREEN); // Right edge (narrow)
  
  // Draw function code (top center) with line leader from top of trapezoid
  int16_t fct_y = 70;
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(80, fct_y);
  tft.print("F=");
  tft.print(alu_f_code, HEX);
  tft.print(" ");  // Space
  tft.print(getALUFunctionName(alu_f_code));
  
  // Line from center-top of trapezoid to just below FCT text
  int16_t fct_text_bottom = fct_y + 21;  // Text size 3 is ~21 pixels tall
  tft.drawLine(trap_top_center_x, trap_top_center_y, trap_top_center_x, fct_text_bottom + 2, ILI9341_YELLOW);
  
  // Draw input B (top left) - OUTSIDE trapezoid
  int16_t b_y = y_top + 30;
  tft.drawLine(10, b_y, x_left, b_y, ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, b_y - 15);
  tft.print("B");
  tft.setCursor(10, b_y + 5);
  char bufB[8];
  sprintf(bufB, "0x%02X", alu_b_value);
  tft.print(bufB);
  
  // Draw input C (bottom left) - OUTSIDE trapezoid
  int16_t c_y = y_bottom - 30;
  tft.drawLine(10, c_y, x_left, c_y, ILI9341_CYAN);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, c_y - 15);
  tft.print("C");
  tft.setCursor(10, c_y + 5);
  char bufC[8];
  sprintf(bufC, "0x%02X", alu_c_value);
  tft.print(bufC);
  
  // Draw output (right side - from narrow end) - RIGHT JUSTIFIED at end of line
  int16_t out_line_end = 230;
  tft.drawLine(x_right, y_center, out_line_end, y_center, ILI9341_MAGENTA);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_MAGENTA);
  
  // Right justify text at x=230 (each char is ~12 pixels wide at size 2)
  // "OUT" = 3 chars = 36 pixels, "0xXX" = 4 chars = 48 pixels
  tft.setCursor(out_line_end - 36, y_center - 15);
  tft.print("OUT");
  tft.setCursor(out_line_end - 48, y_center + 5);
  char bufOut[8];
  sprintf(bufOut, "0x%02X", alu_output);
  tft.print(bufOut);
  
  // Draw "ALU" label in center
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(105, y_center - 15);
  tft.print("ALU");
  
  // Display 74HC181 M and S control bits below trapezoid
  int16_t hct_y = y_bottom + 15;  // Position below trapezoid
  tft.setTextSize(1);  // Smaller font
  tft.setTextColor(ILI9341_ORANGE);
  
  // Extract M bit (bit 4) and S bits (bits 3-0)
  uint8_t m_bit = (hct_ms >> 4) & 0x01;
  uint8_t s_bits = hct_ms & 0x0F;
  
  // Center align under ALU - "74FHCT181" is about 54 pixels at size 1
  tft.setCursor(93, hct_y);
  tft.print("74FHCT181");
  
  // Second line with M and S values
  tft.setCursor(80, hct_y + 10);
  tft.print("M=");
  tft.print(m_bit);
  tft.print(" S=");
  
  // Print S as 4-bit binary
  for (int i = 3; i >= 0; i--) {
    tft.print((s_bits >> i) & 0x01);
  }
  
  // Display Sign, Carry, Zero status flags in lower right
  // Position under OUT area, moved down 2 rows, RIGHT JUSTIFIED
  tft.setTextSize(2);  // Same as B, C, OUT
  
  // Extract status bits
  bool sign_bit = (alu_scz >> 2) & 0x01;   // Bit 2
  bool carry_bit = (alu_scz >> 1) & 0x01;  // Bit 1
  bool zero_bit = alu_scz & 0x01;          // Bit 0
  
  int16_t status_right_edge = 230;  // Right edge of display area
  int16_t status_start_y = y_center + 30 + 36;  // Below OUT value + 2 rows (2 * 18)
  int16_t status_line_height = 18;
  
  // Text size 2: each character is ~12 pixels wide
  // "Sign" = 4 chars = 48 pixels
  // "Carry" = 5 chars = 60 pixels  
  // "Zero" = 4 chars = 48 pixels
  
  // Sign - Green if 1, Red if 0 - RIGHT JUSTIFIED
  tft.setCursor(status_right_edge - 48, status_start_y);
  tft.setTextColor(sign_bit ? ILI9341_GREEN : ILI9341_RED);
  tft.print("Sign");
  
  // Carry - Green if 1, Red if 0 - RIGHT JUSTIFIED
  tft.setCursor(status_right_edge - 60, status_start_y + status_line_height);
  tft.setTextColor(carry_bit ? ILI9341_GREEN : ILI9341_RED);
  tft.print("Carry");
  
  // Zero - Green if 1, Red if 0 - RIGHT JUSTIFIED
  tft.setCursor(status_right_edge - 48, status_start_y + (status_line_height * 2));
  tft.setTextColor(zero_bit ? ILI9341_GREEN : ILI9341_RED);
  tft.print("Zero");
}

// === MCP23017 Functions ===

void mcpWriteReg(uint8_t reg, uint8_t val) {
  if (!mcp_found) return;
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t mcpReadReg(uint8_t reg) {
  if (!mcp_found) return 0xFF;
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(MCP_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void initMCP() {
  Wire.begin(MCP_SDA, MCP_SCL);
  Serial.println("Init MCP23017...");
  Wire.beginTransmission(MCP_ADDR);
  mcp_found = (Wire.endTransmission() == 0);

  if (mcp_found) {
    mcpWriteReg(MCP_IODIRA, 0x00);  // Port A = all outputs (LEDs)
    mcpWriteReg(MCP_IODIRB, 0xFF);  // Port B = all inputs (buttons)
    mcpWriteReg(MCP_GPPUB,  0xFF);  // Port B pull-ups enabled
    mcpWriteReg(MCP_OLATA,  0x00);  // All LEDs off
    Serial.println("MCP23017 OK at 0x20");
  } else {
    Serial.println("MCP23017 NOT FOUND - display only mode");
  }
}

void testLEDs() {
  if (!mcp_found) {
    Serial.println("TEST: MCP not found, skipping");
    return;
  }
  Serial.println("LED walk test - start");
  // Walk forward
  for (int i = 0; i < 8; i++) {
    mcp_led_state = (1 << i);
    mcpWriteReg(MCP_OLATA, mcp_led_state);
    drawMCP();
    delay(150);
  }
  // All on
  mcp_led_state = 0xFF;
  mcpWriteReg(MCP_OLATA, mcp_led_state);
  drawMCP();
  delay(400);
  // All off
  mcp_led_state = 0x00;
  mcpWriteReg(MCP_OLATA, mcp_led_state);
  drawMCP();
  Serial.println("LED walk test - done");
}

void drawMCP() {
  // Clear working area
  tft.fillRect(0, displayStartY, 240, displayHeight, ILI9341_BLACK);

  // MCP status line
  tft.setTextSize(2);
  if (mcp_found) {
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(5, displayStartY + 2);
    tft.print("MCP OK @ 0x20");
  } else {
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(5, displayStartY + 2);
    tft.print("MCP NOT FOUND");
  }

  // LED circles - row of 8
  int16_t led_y = displayStartY + 35;
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(5, led_y - 10);
  tft.print("LEDs:");

  int16_t cx_start = 15;
  int16_t cx_step  = 28;
  int16_t radius   = 11;

  for (int i = 0; i < 8; i++) {
    int16_t cx = cx_start + (i * cx_step);
    bool on = (mcp_led_state >> i) & 0x01;
    uint16_t color = on ? ILI9341_YELLOW : 0x4208;  // yellow or dark grey
    tft.fillCircle(cx, led_y + 5, radius, color);
    tft.drawCircle(cx, led_y + 5, radius, ILI9341_WHITE);
    // LED number
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(cx - 3, led_y + 1);
    tft.print(i + 1);
  }

  // Button circles - row of 8
  int16_t btn_y = led_y + 50;
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(5, btn_y - 10);
  tft.print("BTNs:");

  for (int i = 0; i < 8; i++) {
    int16_t cx = cx_start + (i * cx_step);
    bool pressed = !((mcp_btn_state >> i) & 0x01);  // active LOW
    uint16_t color = pressed ? ILI9341_YELLOW : 0x4208;
    tft.fillCircle(cx, btn_y + 5, radius, color);
    tft.drawCircle(cx, btn_y + 5, radius, ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_BLACK);
    tft.setCursor(cx - 3, btn_y + 1);
    tft.print(i + 1);
  }

  // LED hex value
  int16_t info_y = btn_y + 40;
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(5, info_y);
  char buf[32];
  sprintf(buf, "LED:0x%02X BTN:0x%02X", mcp_led_state, mcp_btn_state);
  tft.print(buf);

  // Last command
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(5, info_y + 25);
  tft.print("CMD:");
  tft.print(lastSerialCmd);

  // Help line
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);  // light grey
  tft.setCursor(5, info_y + 55);
  tft.print("Serial: TEST L1-L8 LA LX B? SCAN");
}

void drawHeader() {
  // Blue header - "Casey RELAY Comp"
  tft.fillRect(0, 0, 240, 25, ILI9341_BLUE);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(72, 5);
  tft.print("ETC 9000");
}

void drawMacBanner() {
  // Cyan MAC banner
  tft.fillRect(0, 25, 240, 23, ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_BLACK);
  tft.setCursor(5, 28);
  tft.print(macAddress);
}