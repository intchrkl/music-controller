#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define PIXEL_PIN   2
#define NUM_PIXELS  1

#define PREV_PIN      8
#define PLAYPAUSE_PIN 7
#define NEXT_PIN      6
#define MUTE_PIN      5
#define POT_PIN       A7

Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Track last button states
bool lastPlayPauseButtonState = HIGH;
bool lastForwardButtonState   = HIGH;
bool lastBackwardButtonState  = HIGH;
bool lastMuteButtonState      = HIGH;

// Potentiometer smoothing / sending
float smoothedPotValue = 0.0;
int lastSentVolume = -1;

const float alpha = 0.1;
const int volumeThreshold = 2;
const unsigned long sendInterval = 50;
unsigned long lastVolumeSendTime = 0;

// LCD states
enum ScreenMode {
  TRACK_SCREEN,
  VOLUME_SCREEN
};

String incomingLine = "";
String currentTrack = "";
String currentArtist = "";

ScreenMode currentScreen = TRACK_SCREEN;
unsigned long lastVolumeInteractionTime = 0;
const unsigned long volumeScreenDuration = 1500; // ms to keep volume screen visible

// LCD screen scrolling
bool scrollingOn = false;

int trackScrollIndex = 0;
int artistScrollIndex = 0;

unsigned long lastScrollTime = 0;
const unsigned long scrollInterval = 700; // ms (adjust speed)
unsigned long scrollStartDelay = 2000;
unsigned long trackDisplayStartTime = 0;

unsigned long scrollPauseUntil = 0;
const unsigned long loopPause = 2000; // pause 1 second at restart

void setup() {
  pinMode(PLAYPAUSE_PIN, INPUT_PULLUP);
  pinMode(NEXT_PIN, INPUT_PULLUP);
  pinMode(PREV_PIN, INPUT_PULLUP);
  pinMode(MUTE_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.clear();
  pixel.show();

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  displayTrackLcd();
}

void drawProgressBar(int percent) {
  int barWidth = 16;
  int filled = map(percent, 0, 100, 0, barWidth);

  lcd.setCursor(0, 1);

  for (int i = 0; i < barWidth; i++) {
    if (i < filled) {
      lcd.write((uint8_t)255);   // full block
    } else {
      lcd.print(" ");
    }
  }
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
  lcd.print("Volume");
  drawProgressBar(vol);
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

String getScrolledText(String text, int index) {
  if (text.length() <= 16) {
    return text;
  }

  String padded = text + "   "; // spacing between loops

  int len = padded.length();

  String result = "";
  for (int i = 0; i < 16; i++) {
    result += padded[(index + i) % len];
  }

  return result;
}

void updateScrolling() {
  if (currentScreen != TRACK_SCREEN) return;
  if (millis() - trackDisplayStartTime < scrollStartDelay) return;

  // pause at the start of a new loop
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

void handleButtons() {
  bool currentPlayPauseButtonState = digitalRead(PLAYPAUSE_PIN);
  bool currentForwardButtonState = digitalRead(NEXT_PIN);
  bool currentBackwardButtonState = digitalRead(PREV_PIN);
  bool currentMuteButtonState = digitalRead(MUTE_PIN);

  // PLAY/PAUSE
  if (lastPlayPauseButtonState == HIGH && currentPlayPauseButtonState == LOW) {
    Serial.println("BTN|PLAYPAUSE");
    pixel.setPixelColor(0, pixel.Color(0, 150, 0));
    pixel.show();
  }
  if (lastPlayPauseButtonState == LOW && currentPlayPauseButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastPlayPauseButtonState = currentPlayPauseButtonState;

  // NEXT
  if (lastForwardButtonState == HIGH && currentForwardButtonState == LOW) {
    Serial.println("BTN|NEXT");
    pixel.setPixelColor(0, pixel.Color(0, 0, 150));
    pixel.show();
  }
  if (lastForwardButtonState == LOW && currentForwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastForwardButtonState = currentForwardButtonState;

  // PREV
  if (lastBackwardButtonState == HIGH && currentBackwardButtonState == LOW) {
    Serial.println("BTN|PREV");
    pixel.setPixelColor(0, pixel.Color(150, 0, 0));
    pixel.show();
  }
  if (lastBackwardButtonState == LOW && currentBackwardButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastBackwardButtonState = currentBackwardButtonState;

  // MUTE
  if (lastMuteButtonState == HIGH && currentMuteButtonState == LOW) {
    Serial.println("BTN|MUTE");
    pixel.setPixelColor(0, pixel.Color(150, 150, 0));
    pixel.show();
  }
  if (lastMuteButtonState == LOW && currentMuteButtonState == HIGH) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
  }
  lastMuteButtonState = currentMuteButtonState;
}

void handleVolume() {
  int rawValue = analogRead(POT_PIN);

  smoothedPotValue = alpha * rawValue + (1.0 - alpha) * smoothedPotValue;
  int smoothValue = (int)smoothedPotValue;

  int volume = map(smoothValue, 300, 4075, 0, 100);
  volume = constrain(volume, 0, 100);

  if (abs(volume - lastSentVolume) >= volumeThreshold) {
    if (millis() - lastVolumeSendTime >= sendInterval) {
      Serial.println("VOL|" + String(volume));
      lastSentVolume = volume;
      lastVolumeSendTime = millis();

      currentScreen = VOLUME_SCREEN;
      lastVolumeInteractionTime = millis();
      displayVolumeLcd(volume);
    }
  }
}

void readSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      incomingLine.trim();
      handleMessage(incomingLine);
      incomingLine = "";
    } else {
      incomingLine += c;
    }
  }
}

void handleMessage(String line) {
  if (line.startsWith("LCD|")) {
    int firstSep = line.indexOf('|');
    int secondSep = line.indexOf('|', firstSep + 1);

    if (secondSep != -1) {
      String track = line.substring(firstSep + 1, secondSep);
      String artist = line.substring(secondSep + 1);

      bool trackChanged = (track != currentTrack) || (artist != currentArtist);

      currentTrack = track;
      currentArtist = artist;

      if (trackChanged) {
        trackScrollIndex = 0;
        artistScrollIndex = 0;
        trackDisplayStartTime = millis();
        lastScrollTime = millis();
      }

      if (currentScreen == TRACK_SCREEN) {
        displayTrackLcd();
      }
    }
  }
}

void loop() {
  handleButtons();
  handleVolume();
  readSerial();
  updateLcdScreen();
  if (scrollingOn) {
    updateScrolling();
  }
  delay(10);
}