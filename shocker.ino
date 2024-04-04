#include <cstdint>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CMD_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define RF_TX 12
#define LIGHT 2

#define MSG_REPEAT_COUNT 6

#define RMT_BIT_COUNT 44

#define CMD_SHOCK 0x01
#define CMD_VIBRATE 0x02
#define CMD_SOUND 0x03

// Measured values:
//   Preamble: 1400us HIGH, 800us LOW
//   One     : 700us  HIGH, 300us LOW
//   Zero    : 200us  HIGH, 800us LOW

const rmt_data_t kRmtPreamble = {1400, 1, 800, 0}; // FC = 11111000
const rmt_data_t kRmtOne      = {700, 1, 300, 0};  // E  = 1110
const rmt_data_t kRmtZero     = {200, 1, 800, 0};  // 8  = 1000

// RF PROTOCOL CONSTANTS
const int txId = 0xEE;
const int channelId = 1;

BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pCharacteristic;

bool cmdReceived;
rmt_data_t rmt_cmd[RMT_BIT_COUNT];

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
    };

    void onDisconnect(BLEServer* pServer) {
      delay(100);
      pServer->startAdvertising();
    }
};

constexpr std::uint8_t checksum(const std::uint32_t payload) {
  std::uint8_t* data = (std::uint8_t*)&payload;
  std::uint8_t checksum = 0;
  checksum += data[0];
  checksum += data[1];
  checksum += data[2];
  checksum += data[3];
  return checksum;
}

void encodeCommand(std::uint64_t data) {
  rmt_cmd[0] = kRmtPreamble;

  for (std::int64_t b = 0; b < 40; b++) {
    int bit_pos = 39-b;
    rmt_cmd[b+1] = (data >> bit_pos) & 1 ? kRmtOne : kRmtZero;
  }

  rmt_cmd[41] = kRmtZero;
  rmt_cmd[42] = kRmtZero;
  rmt_cmd[43] = kRmtZero;
}

void makeTxSequence(int command, int strength) {
  
  switch (command) {
    case CMD_SHOCK:
      strength = std::min(strength, 10);
      break;
    case CMD_VIBRATE:
      strength = std::min(strength, 99);
      break;
    case CMD_SOUND:
      strength = 0;
      break;
    default:
      return;
  }

  // Payload layout: [transmitterId:16][channelId:4][type:4][intensity:8]
  std::uint64_t payload = (static_cast<std::uint32_t>(txId & 0xFFFF) << 16) |
                          (static_cast<std::uint32_t>(channelId & 0xF) << 12) |
                          (static_cast<std::uint32_t>(command & 0xF) << 8) |
                          (static_cast<std::uint32_t>(strength & 0xFF) << 0);

  // Calculate the checksum of the payload
  std::uint8_t cs = checksum(payload);
  payload = (static_cast<std::uint64_t>(payload) << 8) | static_cast<std::uint64_t>(cs);

  encodeCommand(payload);
}

class CmdCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() == 2) {
        makeTxSequence((int)value[0], (int)value[1]);
        cmdReceived = true;
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");

  BLEDevice::init("ShockerBoard");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pChar;
  pChar = pService->createCharacteristic(CMD_UUID, BLECharacteristic::PROPERTY_WRITE);
  pChar->setCallbacks(new CmdCallback());
  
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  pinMode(RF_TX, OUTPUT);
  pinMode(LIGHT, OUTPUT);
  digitalWrite(RF_TX, LOW);
  digitalWrite(LIGHT, LOW);

  if (!rmtInit(RF_TX, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000)) { // 1us tick
    Serial.println("Initialisation Failed");
    return;
  }

  Serial.println("Initialisation Complete");
}

void loop() {
  if (cmdReceived) {
    for (int c=0; c<MSG_REPEAT_COUNT; c++)  {
      rmtWrite(RF_TX, rmt_cmd, RMT_BIT_COUNT, 1000000);
    }

    cmdReceived = false;

    delay(1000);
    digitalWrite(LIGHT, HIGH);
    delay(200);
    digitalWrite(LIGHT, LOW);
  }
}
