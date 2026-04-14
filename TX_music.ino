#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>

#include "USB.h"
#include "USBHIDConsumerControl.h"

USBHIDConsumerControl Consumer;

// ---------------- Pins ----------------
#define PIXEL_PIN   2
#define NUM_PIXELS  1

#define PREV_PIN      8
#define PLAYPAUSE_PIN 7
#define NEXT_PIN      6
#define MUTE_PIN      5

#define ENC_A_PIN     A7
#define ENC_B_PIN     A6

// ---------------- Peripherals ----------------
Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- ESP-NOW ----------------
uint8_t rxMac[] = {0xEC, 0xDA, 0x3B, 0x60, 0x0B, 0x7C};

enum PacketType : uint8_t {
  PKT_COMMAND = 1,
  PKT_LCD     = 2,
  PKT_VOLUME  = 3,
  PKT_PING    = 4,
  PKT_PONG    = 5
};

enum CommandType : uint8_t {
  CMD_PLAY_PAUSE = 1,
  CMD_NEXT       = 2,
  CMD_PREV       = 3,
  CMD_MUTE       = 4,
  CMD_VOL_UP     = 5,
  CMD_VOL_DOWN   = 6
};

struct ControlPacket {
  uint8_t packetType;
  uint8_t command;
};

struct DisplayPacket {
  uint8_t packetType;
  char track[64];
  char artist[64];
};

struct VolumePacket {
  uint8_t packetType;
  uint8_t volumePercent;
};

struct HandshakePacket {
  uint8_t packetType;
};

// ---------------- Modes ----------------
enum OperatingMode {
  MODE_UNKNOWN,
  MODE_WIRELESS,
  MODE_DIRECT
};

OperatingMode currentMode = MODE_UNKNOWN;
bool rxAvailable = false;

// ---------------- Button state ----------------
bool lastPlayPauseButtonState = HIGH;
bool lastForwardButtonState   = HIGH;
bool lastBackwardButtonState  = HIGH;
bool lastMuteButtonState      = HIGH;

// ---------------- Encoder state ----------------
int lastEncAState = HIGH;
unsigned long lastEncoderStepTime = 0;
unsigned long lastVolumeSendTime = 0;
const unsigned long encoderDebounceMs = 3;
const unsigned long sendInterval = 30;

// ---------------- Display state ----------------
int currentSystemVolumePercent = 0;

enum ScreenMode {
  TRACK_SCREEN,
  VOLUME_SCREEN
};

ScreenMode currentScreen = TRACK_SCREEN;

String incomingLine = "";
String currentTrack = "";
String currentArtist = "";

unsigned long lastVolumeInteractionTime = 0;
const unsigned long volumeScreenDuration = 1500;

bool scrollingOn = true;
int trackScrollIndex = 0;
int artistScrollIndex = 0;
unsigned long lastScrollTime = 0;
const unsigned long scrollInterval = 700;
unsigned long scrollStartDelay = 2000;
unsigned long trackDisplayStartTime = 0;
unsigned long scrollPauseUntil = 0;
const unsigned long loopPause = 2000;

// ---------------- Helpers ----------------
void sendConsumerPress(uint16_t key) {
  Consumer.press(key);
  delay(5);
  Consumer.release();
}

void drawProgressBar(int percent) {
  int barWidth = 16;
  int filled = map(percent, 0, 100, 0, barWidth);

  lcd.setCursor(0, 1);
  for (int i = 0; i < barWidth; i++) {
    if (i < filled) {
      lcd.write((uint8_t)255);
    } else {
      lcd.print(" ");
    }
  }
}

String getScrolledText(String text, int index) {
  if (text.length() <= 16) return text;

  String padded = text + "   ";
  int len = padded.length();

  String result = "";
  for (int i = 0; i < 16; i++) {
    result += padded[(index + i) % len];
  }
  return result;
}

void displayTrackLcd() {
  lcd.clear();

  if (currentTrack.length() == 0) {
    lcd.setCursor(0, 0);
    lcd.print("No track yet");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print(getScrolledText(currentTrack, trackScrollIndex));

  lcd.setCursor(0, 1);
  lcd.print(getScrolledText(currentArtist, artistScrollIndex));
}

void displayVolumeLcd(int vol) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Volume ");
  lcd.print(vol);
  lcd.print("%");
  drawProgressBar(vol);
}

void showVolumeOverlay(int percent) {
  currentScreen = VOLUME_SCREEN;
  lastVolumeInteractionTime = millis();
  displayVolumeLcd(percent);
}

void updateLcdScreen() {
  if (currentScreen == VOLUME_SCREEN) {
    if (millis() - lastVolumeInteractionTime >= volumeScreenDuration) {
      currentScreen = TRACK_SCREEN;
      trackDisplayStartTime = millis();
      lastScrollTime = millis();
      displayTrackLcd();
    }
  }
}

void updateScrolling() {
  if (currentScreen != TRACK_SCREEN) return;
  if (millis() - trackDisplayStartTime < scrollStartDelay) return;
  if (millis() < scrollPauseUntil) return;
  if (millis() - lastScrollTime < scrollInterval) return;

  bool wrapped = false;

  if (currentTrack.length() > 16) {
    int trackLen = (currentTrack + "   ").length();
    trackScrollIndex = (trackScrollIndex + 1) % trackLen;
    if (trackScrollIndex == 0) wrapped = true;
  }

  if (currentArtist.length() > 16) {
    int artistLen = (currentArtist + "   ").length();
    artistScrollIndex = (artistScrollIndex + 1) % artistLen;
    if (artistScrollIndex == 0) wrapped = true;
  }

  lastScrollTime = millis();
  displayTrackLcd();

  if (wrapped) {
    scrollPauseUntil = millis() + loopPause;
  }
}

void sendCommandWireless(uint8_t cmd) {
  ControlPacket pkt;
  pkt.packetType = PKT_COMMAND;
  pkt.command = cmd;
  esp_now_send(rxMac, (uint8_t*)&pkt, sizeof(pkt));
}

void sendPing() {
  HandshakePacket pkt;
  pkt.packetType = PKT_PING;
  esp_now_send(rxMac, (uint8_t*)&pkt, sizeof(pkt));
}

void handleMediaCommand(uint8_t cmd) {
  if (currentMode == MODE_WIRELESS) {
    sendCommandWireless(cmd);
    return;
  }

  switch (cmd) {
    case CMD_PLAY_PAUSE:
      sendConsumerPress(HID_USAGE_CONSUMER_PLAY_PAUSE);
      break;
    case CMD_NEXT:
      sendConsumerPress(HID_USAGE_CONSUMER_SCAN_NEXT);
      break;
    case CMD_PREV:
      sendConsumerPress(HID_USAGE_CONSUMER_SCAN_PREVIOUS);
      break;
    case CMD_MUTE:
      sendConsumerPress(HID_USAGE_CONSUMER_MUTE);
      break;
    case CMD_VOL_UP:
      sendConsumerPress(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
      break;
    case CMD_VOL_DOWN:
      sendConsumerPress(HID_USAGE_CONSUMER_VOLUME_DECREMENT);
      break;
  }
}

void handleButtons() {
  bool currentPlayPauseButtonState = digitalRead(PLAYPAUSE_PIN);
  bool currentForwardButtonState   = digitalRead(NEXT_PIN);
  bool currentBackwardButtonState  = digitalRead(PREV_PIN);
  bool currentMuteButtonState      = digitalRead(MUTE_PIN);

  if (lastPlayPauseButtonState == HIGH && currentPlayPauseButtonState == LOW) {
    handleMediaCommand(CMD_PLAY_PAUSE);
    pixel.setPixelColor(0, pixel.Color(0, 150, 0));
    pixel.show();
  }
  if (lastPlayPauseButtonState == LOW && currentPlayPauseButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastPlayPauseButtonState = currentPlayPauseButtonState;

  if (lastForwardButtonState == HIGH && currentForwardButtonState == LOW) {
    handleMediaCommand(CMD_NEXT);
    pixel.setPixelColor(0, pixel.Color(0, 0, 150));
    pixel.show();
  }
  if (lastForwardButtonState == LOW && currentForwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastForwardButtonState = currentForwardButtonState;

  if (lastBackwardButtonState == HIGH && currentBackwardButtonState == LOW) {
    handleMediaCommand(CMD_PREV);
    pixel.setPixelColor(0, pixel.Color(150, 0, 0));
    pixel.show();
  }
  if (lastBackwardButtonState == LOW && currentBackwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastBackwardButtonState = currentBackwardButtonState;

  if (lastMuteButtonState == HIGH && currentMuteButtonState == LOW) {
    handleMediaCommand(CMD_MUTE);
    pixel.setPixelColor(0, pixel.Color(150, 150, 0));
    pixel.show();
    showVolumeOverlay(currentSystemVolumePercent);
  }
  if (lastMuteButtonState == LOW && currentMuteButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastMuteButtonState = currentMuteButtonState;
}

void handleVolume() {
  int currentA = digitalRead(ENC_A_PIN);

  if (lastEncAState == HIGH && currentA == LOW) {
    unsigned long now = millis();

    if (now - lastEncoderStepTime >= encoderDebounceMs &&
        now - lastVolumeSendTime >= sendInterval) {

      int currentB = digitalRead(ENC_B_PIN);

      if (currentB == HIGH) {
        handleMediaCommand(CMD_VOL_UP);
      } else {
        handleMediaCommand(CMD_VOL_DOWN);
      }

      lastEncoderStepTime = now;
      lastVolumeSendTime = now;

      showVolumeOverlay(currentSystemVolumePercent);
    }
  }

  lastEncAState = currentA;
}

void handleDisplayPacket(const DisplayPacket& pkt) {
  String newTrack = String(pkt.track);
  String newArtist = String(pkt.artist);
  bool changed = (newTrack != currentTrack) || (newArtist != currentArtist);

  currentTrack = newTrack;
  currentArtist = newArtist;

  if (changed) {
    trackScrollIndex = 0;
    artistScrollIndex = 0;
    trackDisplayStartTime = millis();
    lastScrollTime = millis();
    scrollPauseUntil = 0;
  }

  if (currentScreen == TRACK_SCREEN) {
    displayTrackLcd();
  }
}

void handleVolumePacket(const VolumePacket& pkt) {
  currentSystemVolumePercent = constrain(pkt.volumePercent, 0, 100);

  if (currentScreen == VOLUME_SCREEN) {
    displayVolumeLcd(currentSystemVolumePercent);
  }
}

void handleSerialMessage(String line) {
  if (line.startsWith("LCD|")) {
    int firstSep = line.indexOf('|');
    int secondSep = line.indexOf('|', firstSep + 1);

    if (secondSep != -1) {
      DisplayPacket pkt = {};
      pkt.packetType = PKT_LCD;

      String track = line.substring(firstSep + 1, secondSep);
      String artist = line.substring(secondSep + 1);

      track.toCharArray(pkt.track, sizeof(pkt.track));
      artist.toCharArray(pkt.artist, sizeof(pkt.artist));

      handleDisplayPacket(pkt);
    }
  } else if (line.startsWith("VOL|")) {
    VolumePacket pkt;
    pkt.packetType = PKT_VOLUME;
    pkt.volumePercent = constrain(line.substring(4).toInt(), 0, 100);
    handleVolumePacket(pkt);
  }
}

void readSerialDirectMode() {
  if (currentMode != MODE_DIRECT) return;

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      incomingLine.trim();
      if (incomingLine.length() > 0) {
        handleSerialMessage(incomingLine);
      }
      incomingLine = "";
    } else {
      incomingLine += c;
    }
  }
}

// Older ESP32 core callback signature
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  uint8_t packetType = data[0];

  if (packetType == PKT_PONG && len == sizeof(HandshakePacket)) {
    rxAvailable = true;
    return;
  }

  if (currentMode != MODE_WIRELESS) return;

  if (packetType == PKT_LCD && len == sizeof(DisplayPacket)) {
    DisplayPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    handleDisplayPacket(pkt);
  } else if (packetType == PKT_VOLUME && len == sizeof(VolumePacket)) {
    VolumePacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    handleVolumePacket(pkt);
  }
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, rxMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add RX peer");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onDataRecv);
}

void detectMode() {
  rxAvailable = false;

  unsigned long start = millis();
  unsigned long lastPing = 0;

  while (millis() - start < 1000) {
    if (millis() - lastPing >= 100) {
      sendPing();
      lastPing = millis();
    }

    if (rxAvailable) {
      currentMode = MODE_WIRELESS;
      return;
    }

    delay(5);
  }

  currentMode = MODE_DIRECT;
}

void setup() {
  Serial.begin(115200);

  pinMode(PLAYPAUSE_PIN, INPUT_PULLUP);
  pinMode(NEXT_PIN, INPUT_PULLUP);
  pinMode(PREV_PIN, INPUT_PULLUP);
  pinMode(MUTE_PIN, INPUT_PULLUP);
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.clear();
  pixel.show();

  lcd.init();
  lcd.backlight();
  displayTrackLcd();

  lastEncAState = digitalRead(ENC_A_PIN);

  // Start USB so direct mode can work if selected
  USB.begin();
  Consumer.begin();

  setupEspNow();
  detectMode();

  Serial.print("TX WiFi MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Mode: ");
  if (currentMode == MODE_WIRELESS) {
    Serial.println("WIRELESS");
  } else {
    Serial.println("DIRECT");
  }
}

void loop() {
  handleButtons();
  handleVolume();
  readSerialDirectMode();
  updateLcdScreen();

  if (scrollingOn) {
    updateScrolling();
  }

  delay(2);
}