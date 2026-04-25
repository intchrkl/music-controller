#include <WiFi.h>
#include <esp_now.h>

#include "USB.h"
#include "USBHIDConsumerControl.h"

USBHIDConsumerControl Consumer;

// ---------------- ESP-NOW ----------------
uint8_t txMac[] = {0x3C, 0x84, 0x27, 0xC3, 0xFD, 0xF0};

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

String incomingLine = "";

// Executes the keypress when a button is pressed on the TX
void sendConsumerPress(uint16_t key) {
  Consumer.press(key);
  delay(5);
  Consumer.release();
}

// Initial reply to TX to establish connection
void sendPong() {
  HandshakePacket pkt;
  pkt.packetType = PKT_PONG;
  esp_now_send(txMac, (uint8_t*)&pkt, sizeof(pkt));
}

// Sends current track commands to TX to display currently playing track on LCD screen
void sendDisplayPacket(const String& track, const String& artist) {
  DisplayPacket pkt = {};
  pkt.packetType = PKT_LCD;

  track.substring(0, sizeof(pkt.track) - 1).toCharArray(pkt.track, sizeof(pkt.track));
  artist.substring(0, sizeof(pkt.artist) - 1).toCharArray(pkt.artist, sizeof(pkt.artist));

  esp_now_send(txMac, (uint8_t*)&pkt, sizeof(pkt));
}

// Sends volume commands back to TX to display volume levels on LCD screen
void sendVolumePacket(int vol) {
  VolumePacket pkt;
  pkt.packetType = PKT_VOLUME;
  pkt.volumePercent = constrain(vol, 0, 100);

  esp_now_send(txMac, (uint8_t*)&pkt, sizeof(pkt));
}

// Receives LCD commands from python script and forwards to TX
void handleSerialMessage(String line) {
  if (line.startsWith("LCD|")) {
    int firstSep = line.indexOf('|');
    int secondSep = line.indexOf('|', firstSep + 1);

    if (secondSep != -1) {
      String track = line.substring(firstSep + 1, secondSep);
      String artist = line.substring(secondSep + 1);
      sendDisplayPacket(track, artist);
    }
  } else if (line.startsWith("VOL|")) {
    int vol = line.substring(4).toInt();
    sendVolumePacket(vol);
  }
}

// Parses serial port
void readSerial() {
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

  if (packetType == PKT_PING && len == sizeof(HandshakePacket)) {
    sendPong();
    return;
  }

  if (packetType != PKT_COMMAND || len != sizeof(ControlPacket)) return;

  ControlPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  switch (pkt.command) {
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

void setupEspNow() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, txMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add TX peer");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onDataRecv);
}

void setup() {
  Serial.begin(115200);

  USB.begin();
  Consumer.begin();

  setupEspNow();

  Serial.print("RX WiFi MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  readSerial();
  delay(2);
}