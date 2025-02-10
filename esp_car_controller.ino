/////////////////////////////////////////////////////////// PACKAGES / LIBRARIES

#include <esp_car.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_ST7735.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <JPEGDEC.h>
#include <Preferences.h>

// Some ready-made 16-bit ('565') color settings:
#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define ST77XX_RED 0xF800
#define ST77XX_GREEN 0x07E0
#define ST77XX_BLUE 0x001F
#define ST77XX_CYAN 0x07FF
#define ST77XX_MAGENTA 0xF81F
#define ST77XX_YELLOW 0xFFE0
#define ST77XX_ORANGE 0xFC00

/////////////////////////////////////////////////////////// CONSTANTS

const uint8_t espCarAddress[] = {0xEC, 0x64, 0xC9, 0x5E, 0x1E, 0x8C};

//const uint8_t TFT_CS_PIN = 17;
//const uint8_t TFT_RST_PIN = 21;
//const uint8_t TFT_RS_PIN = 16;
const uint8_t JOYSTICK_DRIVE_X_PIN = 34;
const uint8_t JOYSTICK_DRIVE_Y_PIN = 35;
const uint8_t JOYSTICK_DRIVE_BUTTON_PIN = 27;
const uint8_t JOYSTICK_TURN_X_PIN = 36;
const uint8_t JOYSTICK_TURN_Y_PIN = 39;
const uint8_t JOYSTICK_TURN_BUTTON_PIN = 4;
const uint8_t DPAD_CENTER_PIN = 22;
const uint8_t DPAD_UP_PIN = 32;
const uint8_t DPAD_RIGHT_PIN = 33;
const uint8_t DPAD_DOWN_PIN = 14;
const uint8_t DPAD_LEFT_PIN = 13;

const uint16_t JOYSTICK_DEADZONE_UPPER = 2200;
const uint16_t JOYSTICK_DEADZONE_LOWER = 1500;

/////////////////////////////////////////////////////////// OBJECTS

//Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS_PIN, TFT_RS_PIN, TFT_RST_PIN);
TFT_eSPI tft = TFT_eSPI();

JPEGDEC jpeg;

Preferences prefs;

/////////////////////////////////////////////////////////// STRUCTS

struct Button {
  uint8_t pinNum;
  unsigned long heldFor;
  bool toggled;
};
Button buttons[] = {
  {DPAD_CENTER_PIN, 0, false},
  {DPAD_UP_PIN, 0, false},
  {DPAD_RIGHT_PIN, 0, false},
  {DPAD_DOWN_PIN, 0, false},
  {DPAD_LEFT_PIN, 0, false}
};

struct TFTMenuManager {
  bool opened = false;
  bool queuedForOpen = false;
  uint8_t serialOpened = 0;
  uint8_t categoryIndex = 0;
};
TFTMenuManager menuManager;

struct TFTMenu {
  const char * categoryType;
  const char * title;
  const uint8_t maxValue;
  uint8_t * carConfigValue;
};
TFTMenu menuList[] = {
  {"LED", "Red", 255, &carConfig.r},
  {"LED", "Green", 255, &carConfig.g},
  {"LED", "Blue", 255, &carConfig.b},

  {"CAMERA", "Quality", 2, &carConfig.camQuality},
  {"CAMERA", "Night Mode", 1, &carConfig.camNightMode},

  {"MOVEMENT", "Speed %", 100, &carConfig.speedPerc},
  {"MOVEMENT", "Turn %", 100, &carConfig.turnPerc},

  {"SERIAL", "Enabled", 1, &menuManager.serialOpened}
};

struct TFTCamFrame {
  const uint8_t totalPacketSize = 128;
  const uint8_t totalPacketReservedSize = 2;

  bool currentlyDrawing = false;
  uint8_t storedBytes[10000];
  uint32_t nextIndexOfByteToStore = 0;
  uint8_t nextChunkNumber = 0;
  unsigned long lastFrameTimer = 1;
  bool lostFeed = true;
};
TFTCamFrame camFrame;

/////////////////////////////////////////////////////////// FUNCTIONS

uint16_t rgb888_to_rgb565(uint8_t R, uint8_t G, uint8_t B) {
  uint16_t r_565 = (R >> 3) & 0x1F; // Red component
  uint16_t g_565 = (G >> 2) & 0x3F; // Green component
  uint16_t b_565 = (B >> 3) & 0x1F; // Blue component
  
  return (r_565 << 11) | (g_565 << 5) | b_565;
}

int rollingClamp(int value, int maxVal) {
  if (value > maxVal) {
    return 0;
  }
  else if (value < 0) {
    return maxVal;
  }
  return value;
}

TFTMenu * getCurrentMenu() {
  return &menuList[menuManager.categoryIndex];
}

void drawCenteredText(uint8_t size, int16_t x, int16_t y, const char * text) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;

  tft.setTextSize(size);

  //tft.getTextBounds(text, x, y, &x1, &y1, &w, &h);

  tft.setCursor(x - (tft.textWidth(text) / 2), y - (tft.fontHeight() / 2));

  //tft.setCursor(x1 - (w / 2), y1 - (h / 2));
  tft.print(text);
}
void drawCenteredText(int16_t x, int16_t y, const char * text) {
  drawCenteredText(1, x, y, text);
}

void drawMenu() {
  // Check cam frame for drawing first as it's done in other thread or core or some shit idk and text gets jumbled
  if (camFrame.currentlyDrawing) {
    menuManager.queuedForOpen = true;
    return;
  }
  else {
    menuManager.opened = true;
  }

  TFTMenu * currentMenu = getCurrentMenu();

  tft.fillScreen(ST77XX_BLUE);
  drawCenteredText(3, 80, 20, currentMenu->categoryType);
  tft.drawFastHLine(16, 36, 128, ST77XX_WHITE);
  drawCenteredText(2, 80, 50, currentMenu->title);
  char configValueBuf[4];
  sprintf(configValueBuf, "%d", *currentMenu->carConfigValue);
  drawCenteredText(3, 80, 90, configValueBuf);
}

void modifyMenuCategory(int mod) {
  menuManager.categoryIndex = (uint8_t)rollingClamp((int)menuManager.categoryIndex + mod, (int)(sizeof(menuList) / sizeof(TFTMenu)) - 1);
  drawMenu();
}

void modifyMenuOptionValue(int mod) {
  TFTMenu * currentMenu = getCurrentMenu();

  *currentMenu->carConfigValue = (uint8_t)rollingClamp((int)*currentMenu->carConfigValue + mod, (int)currentMenu->maxValue);
  drawMenu();

  uint8_t configBytes[] = {3, menuManager.categoryIndex, *currentMenu->carConfigValue};
  SendReliablePacketToCar(configBytes);
}

void resetScreenForCam() {
  menuManager.opened = false;
  camFrame.lostFeed = true;

  uint16_t colorFromLED = rgb888_to_rgb565(carConfig.r, carConfig.g, carConfig.b);
  tft.fillScreen(colorFromLED);
  tft.fillRect(16, 0, 128, 128, ST77XX_BLUE);
  drawCenteredText(80, 64, "WAITING FOR");
  drawCenteredText(80, 80, "CAM FEED");
}

void doButtonPress(int btnIndex) {
  switch (btnIndex) {
    case 0:
      if (menuManager.opened) {
        resetScreenForCam();
      }
      else {
        drawMenu();
      }
      break;
    case 1: // up
      if (menuManager.opened) {
        modifyMenuOptionValue(1);
      }
      else {
        uint8_t sndIndexBytes[] = {2, 0};
        SendReliablePacketToCar(sndIndexBytes);
      }
      break;
    case 2: // right
      if (menuManager.opened) {
        modifyMenuCategory(1);
      }
      else {
        uint8_t sndIndexBytes[] = {2, 1};
        SendReliablePacketToCar(sndIndexBytes);
      }
      break;
    case 3: // down
      if (menuManager.opened) {
        modifyMenuOptionValue(-1);
      }
      else {
        uint8_t sndIndexBytes[] = {2, 2};
        SendReliablePacketToCar(sndIndexBytes);
      }
      break;
    case 4: // left
      if (menuManager.opened) {
        modifyMenuCategory(-1);
      }
      else {
        uint8_t sndIndexBytes[] = {2, 3};
        SendReliablePacketToCar(sndIndexBytes);
      }
      break;
  }
}

void doButtonHold(int btnIndex) {
  switch (btnIndex) {
    case 1: // up
      if (menuManager.opened) {
        modifyMenuOptionValue(1);
      }
      break;
    case 2: // right
      if (menuManager.opened) {
        modifyMenuCategory(1);
      }
      break;
    case 3: // down
      if (menuManager.opened) {
        modifyMenuOptionValue(-1);
      }
      break;
    case 4: // left
      if (menuManager.opened) {
        modifyMenuCategory(-1);
      }
      break;
  }
}

int JPEGDraw(JPEGDRAW * pDraw) {
  tft.setAddrWindow(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
  //tft.writePixels(pDraw->pPixels, pDraw->iWidth * pDraw->iHeight, true, false);
  tft.pushColors(pDraw->pPixels, pDraw->iWidth * pDraw->iHeight, true);
  return 1;
} /* JPEGDraw() */

bool drawJPG() {
  // Skip frame if beginning signature not found
  if (camFrame.storedBytes[0] != 255 && camFrame.storedBytes[1] != 216) {
    return false;
  }

  // Skip frame if end signature not found
  bool endSigFound = false;
  uint8_t lastByteForEndSig = 0;
  for (uint32_t i = camFrame.nextIndexOfByteToStore - 1; i >= camFrame.nextIndexOfByteToStore - camFrame.totalPacketSize; i--) {
    uint8_t thisByte = camFrame.storedBytes[i];
    if (thisByte == 255 && lastByteForEndSig == 217) {
      endSigFound = true;
      break;
    }
    else {
      lastByteForEndSig = thisByte;
    }
  }
  if (!endSigFound) {
    return false;
  }

  camFrame.currentlyDrawing = true;
  tft.startWrite();

  // Try opening, around 250-300 microseconds tested
  bool jpgOpened = jpeg.openRAM(camFrame.storedBytes, (int)camFrame.nextIndexOfByteToStore, JPEGDraw);
  // If opened, then do drawing, around 12000 microseconds tested
  if (jpgOpened) {
    jpeg.decode(16, 0, 0);
    jpeg.close();
  }

  //tft.dmaWait();
  tft.endWrite();

  if (jpgOpened) {
    double frameFPS = 1000000.0 / camFrame.lastFrameTimer;
    camFrame.lastFrameTimer = 1;
    camFrame.lostFeed = false;
    
    tft.setCursor(16, 0);
    tft.setTextSize(1);

    char frameFPSBuf[5];
    sprintf(frameFPSBuf, "%.2f", frameFPS);
    tft.print(frameFPSBuf);
  }

  camFrame.currentlyDrawing = false;

  return jpgOpened;
}

/////////////////////////////////////////////////////////// SETUP AND LOOP (MAIN)

// Time variables for loop (SET AT END OF setup)
unsigned long oldTime = 0;
void setup() {
  //tft.initR(INITR_BLACKTAB);
  tft.init();
  tft.setRotation(45);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextWrap(false);
  tft.setCursor(0, 0);

  tft.println("DISPLAY STARTED");

  tft.println("INITIALIZING PINS");

  // Left (drive) stick
  pinMode(JOYSTICK_DRIVE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOYSTICK_DRIVE_X_PIN, INPUT);
  pinMode(JOYSTICK_DRIVE_Y_PIN, INPUT);

  // Right (turn) stick
  pinMode(JOYSTICK_TURN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOYSTICK_TURN_X_PIN, INPUT);
  pinMode(JOYSTICK_TURN_Y_PIN, INPUT);

  pinMode(DPAD_CENTER_PIN, INPUT_PULLUP);
  pinMode(DPAD_UP_PIN, INPUT_PULLUP);
  pinMode(DPAD_RIGHT_PIN, INPUT_PULLUP);
  pinMode(DPAD_DOWN_PIN, INPUT_PULLUP);
  pinMode(DPAD_LEFT_PIN, INPUT_PULLUP);

  tft.println("INITIALIZING CONFIG PREFS");

  prefs.begin("espcarcontroller");
  

  Serial.begin(600000);
  tft.println("DELAYING FOR SERIAL");

  delay(1000);

  tft.println("STARTING ESP-NOW");

  // Get init ESP-NOW results
  char initEspNowPrintMsgBuf[50];
  bool initEspNowSuccess = initEspNow(espCarAddress, OnPacketReceived, initEspNowPrintMsgBuf);

  // Print results and stop device setup if result code is an error
  tft.println(initEspNowPrintMsgBuf);
  Serial.println(initEspNowPrintMsgBuf);
  if (!initEspNowSuccess) {
    return;
  }
  
  tft.println("DEVICE READY :)");
  resetScreenForCam();

  // SET LOOP TIME AT END OF setup
  oldTime = micros();
}

unsigned long sendMoveTimer = 0;
void loop() {
  unsigned long currentTime = micros();
  unsigned long dt = currentTime - oldTime;
  oldTime = currentTime;

  // Iterate button pins
  for (int i = 0; i < sizeof(buttons) / sizeof(Button); i++) {
    uint8_t pinNum = buttons[i].pinNum;
    int buttonState = digitalRead(pinNum);

    // If button pressed (LOW)
    if (buttonState == LOW) {
      if (buttons[i].toggled == false && buttons[i].heldFor >= 20000) {
        // Toggled on first time after being off
        buttons[i].toggled = true;
        doButtonPress(i);
      }
      else if (buttons[i].heldFor >= 250000) {
        // Toggled on and held for 3 seconds or more
        doButtonHold(i);
      }
      buttons[i].heldFor += dt;
    }
    else {
      if (buttons[i].heldFor > 0) {
        buttons[i].toggled = false;
        buttons[i].heldFor = 0;
      }
    }
  }

  // Stop here if menu is open to not send any inputs
  if (menuManager.opened) {
    return;
  }

  // Try open menus if it's queued for open, this is to wait for cam frame
  if (menuManager.queuedForOpen && !camFrame.currentlyDrawing) {
    menuManager.queuedForOpen = false;
    drawMenu();
    return;
  }

  camFrame.lastFrameTimer += dt;

  if (!camFrame.lostFeed && camFrame.lastFrameTimer >= 5000000) {
    resetScreenForCam();
  }

  sendMoveTimer += dt;
  if (sendMoveTimer >= 15000) {
    sendMoveTimer = 0;

    // Read joysticks
    uint16_t driveStickY = analogRead(JOYSTICK_DRIVE_Y_PIN);
    uint16_t turnStickX = analogRead(JOYSTICK_TURN_X_PIN);
    uint8_t speedScale = 100;
    uint8_t turnScale = 100;
    if (carConfig.speedPerc > 0) {
      if (driveStickY <= JOYSTICK_DEADZONE_LOWER) {
        speedScale = (uint8_t)lerp(0.0, 100.0, (float)driveStickY / (float)JOYSTICK_DEADZONE_LOWER);
      }
      else if (driveStickY >= JOYSTICK_DEADZONE_UPPER) {
        speedScale = (uint8_t)lerp(100.0, 200.0, ((float)driveStickY - (float)JOYSTICK_DEADZONE_UPPER) / (4095.0 - (float)JOYSTICK_DEADZONE_UPPER));
      }
    }

    if (carConfig.turnPerc > 0) {
      if (turnStickX <= JOYSTICK_DEADZONE_LOWER) {
        turnScale = (uint8_t)lerp(0.0, 100.0, (float)turnStickX / (float)JOYSTICK_DEADZONE_LOWER);
      }
      else if (turnStickX >= JOYSTICK_DEADZONE_UPPER) {
        turnScale = (uint8_t)lerp(100.0, 200.0, ((float)turnStickX - (float)JOYSTICK_DEADZONE_UPPER) / (4095.0 - (float)JOYSTICK_DEADZONE_UPPER));
      }
    }

    if (speedScale != 100 || turnScale != 100) {
      // Adjust speedScale to percentage in config
      speedScale = (uint8_t)(((double)speedScale - 100.0) * ((double)carConfig.speedPerc / 100.0) + 100.0);

      // Adjust turnScale to percentage in config
      turnScale = (uint8_t)(((double)turnScale - 100.0) * ((double)carConfig.turnPerc / 100.0) + 100.0);

      uint8_t driveBytes[] = {0, 250, speedScale, turnScale};
      BroadcastPacket(driveBytes);
    }

    if (digitalRead(JOYSTICK_TURN_BUTTON_PIN) == LOW) {
      uint16_t joystickRead = analogRead(JOYSTICK_TURN_Y_PIN);
      uint8_t hornHzScale = 5;
      if (joystickRead >= JOYSTICK_DEADZONE_UPPER)
        hornHzScale = 0;
      else if (joystickRead <= JOYSTICK_DEADZONE_LOWER)
        hornHzScale = 10;
      uint8_t honkBytes[] = {1, 100, hornHzScale, 50};
      BroadcastPacket(honkBytes);
    }
    else if (digitalRead(JOYSTICK_DRIVE_BUTTON_PIN) == LOW) {
      uint8_t hornHzScale = (uint8_t)lerp(100.0, 0.0, (float)analogRead(JOYSTICK_DRIVE_X_PIN) / 4095.0);
      uint8_t honkBytes[] = {1, 100, hornHzScale, 50};
      BroadcastPacket(honkBytes);
    }
  }
}

/////////////////////////////////////////////////////////// NETWORKING (ESP-NOW)

void readCamFrameChunkPacket(const uint8_t* packet, int len) {
  if (menuManager.opened) {
    // Reset first byte to 0 to stop broken frame from being drawn
    camFrame.storedBytes[0] = 0;
    return;
  }

  uint8_t chunkNum = packet[1];

  if (chunkNum == 0) {
    // Time to display the JPG
    drawJPG();

    // Then reset camFrame vars
    memset(camFrame.storedBytes, 0, 10000);
    camFrame.nextChunkNumber = 0;
    camFrame.nextIndexOfByteToStore = 0;
  }

  if (chunkNum != camFrame.nextChunkNumber) {
    return;
  }

  uint8_t totalCamBytes = camFrame.totalPacketSize - camFrame.totalPacketReservedSize;
  memcpy(&camFrame.storedBytes[camFrame.nextIndexOfByteToStore], &packet[camFrame.totalPacketReservedSize], totalCamBytes * sizeof(uint8_t));
  camFrame.nextIndexOfByteToStore += totalCamBytes;

  camFrame.nextChunkNumber++;

  if (menuManager.serialOpened == 1) {
    Serial.write(&packet[camFrame.totalPacketReservedSize], (size_t)totalCamBytes);
  }
}

void OnPacketReceived(const esp_now_recv_info_t * espInfo, const uint8_t * packet, int len) {
  // Check if packet from car address
  for (int i = 0; i < 6; i++) {
    if (espInfo->src_addr[i] != espCarAddress[i]) {
      return;
    }
  }

  // Process packet from car
  switch (packet[0]) {
    case 0:
      readCamFrameChunkPacket(packet, len);
      break;
  }
}

esp_err_t BroadcastPacket(uint8_t * bytesToSend) {
  return esp_now_send(broadcastAddress, bytesToSend, sizeof(bytesToSend) / sizeof(uint8_t));
}

esp_err_t SendPacketToCar(uint8_t * bytesToSend) {
  return esp_now_send(espCarAddress, bytesToSend, sizeof(bytesToSend) / sizeof(uint8_t));
}

esp_err_t SendReliablePacketToCar(uint8_t * bytesToSend) {
  // Make sure to wait until packet can send due to NO_MEM thing
  esp_err_t packetSendResult;
  do {
    packetSendResult = SendPacketToCar(bytesToSend);
  } while (packetSendResult == ESP_ERR_ESPNOW_NO_MEM);

  return packetSendResult;
}