// ESP32 BLE Buzzer - receives melody frames via Bluetooth LE
// Frame format: 4 bytes per note (uint16 freq_hz + uint16 dur_ms, little-endian)
// Touch pin 4 to pause/resume playback

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Pins
#define BUZZER_PIN 16
#define LED_R_PIN 23
#define LED_G_PIN 22
#define LED_B_PIN 1
#define TOUCH_PIN 4
#define TOUCH_THRESHOLD 30

// BLE UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHAR_DATA_UUID      "12345678-1234-1234-1234-123456789abd"  // Write frames here
#define CHAR_CONTROL_UUID   "12345678-1234-1234-1234-123456789abe"  // Control commands

// Max notes (40KB buffer = 10000 notes * 4 bytes)
#define MAX_NOTES 10000

// Default melody (lechuck in mi1)
int defaultMelody[] = {
  392, 0, 392, 0, 466, 0, 587, 0, 98, 0, 392, 0, 466, 0, 392, 0, 466, 0, 587, 0, 554, 0, 440, 0, 554, 0, 659, 0, 554, 0, 110, 0, 262, 0, 440, 0, 262, 440, 0, 440, 0, 554, 0, 659, 0, 262, 0, 262, 0, 311, 0, 392, 0, 622, 0, 311, 0, 78, 0, 311, 0, 392, 0, 622, 0, 294, 0, 440, 0, 587, 0, 294, 0, 73, 0, 262, 0, 294, 0, 262, 294, 0, 440, 0, 587, 0, 262, 0, 262, 0, 392, 0, 392, 0, 587, 0, 98, 0, 392, 0, 466, 0, 392, 0, 587, 0, 659, 0, 440, 0, 554, 0, 659, 0, 110, 0, 262, 0, 554, 0, 262, 554, 0, 440, 0, 554, 0, 659, 0, 262, 0, 262, 0, 262, 0, 392, 0, 440, 0, 523, 0, 262, 0, 131, 0, 262, 0, 392, 0, 523, 0, 262, 0, 294, 0, 440, 0, 587, 0, 147, 0, 262, 147, 0, 262, 0, 440, 0, 587, 0, 262, 0, 233, 0, 392, 0, 392, 0, 587, 0, 98, 0, 392, 0, 466, 0, 392, 0, 587, 0, 554, 0, 440, 0, 554, 0, 659, 0, 554, 0, 110, 0, 262, 0, 440, 0, 262, 440, 0, 440, 0, 554, 0, 659, 0, 262, 0, 262, 0, 262, 0, 392, 0, 523, 0, 622, 0, 262, 0, 131, 0, 262, 0, 392, 0, 523, 0, 622, 0, 233, 0, 196, 0, 392, 0, 587, 0, 196, 0, 98, 0, 262, 0, 196, 262, 196, 0, 392, 0, 587, 0, 262, 0, 392, 0, 392, 0, 622, 0, 392, 0, 262, 392, 0, 156, 0, 440, 0, 262, 0, 262, 0, 622, 0, 466, 0, 440, 0, 440, 0, 587, 0, 370, 0, 262, 0, 147, 0, 294, 0, 262, 294, 0, 262, 587, 0, 294, 0, 262, 0, 523, 0, 262, 0, 262, 131, 0, 294, 0, 262, 0, 262, 523, 0, 311, 0, 294, 0, 440, 0, 587, 0, 262, 0, 147, 0, 294, 0, 262, 294, 0, 262, 0, 587, 0, 294, 0, 262, 0, 311, 0, 392, 523, 0, 262, 0, 262, 0, 131, 0, 294, 0, 262, 0, 262, 523, 0, 311, 0, 294, 0, 440, 0, 587, 0, 147, 0, 294, 0, 262, 0, 262, 0, 262, 587, 0, 233, 0, 220, 0, 196, 0, 392, 0, 587, 0, 98, 0, 262, 0, 392, 0, 587, 0, 233, 0, 294, 0, 392, 0, 156, 0, 392, 0, 622, 0, 392, 0, 370, 0, 147, 0, 440, 0, 587, 0, 370, 0, 392, 0, 392, 0, 587, 0, 98, 0, 262, 0, 196, 0, 262, 0, 392, 0, 587, 0, 196, 0, 262, 0, 262, 0, 117, 0, 392, 0, 587, 0, 196, 0, 147, 0, 196, 0, 392, 0, 466, 0, 587, 0, 117, 0, 196, 0, 392, 466, 0, 587, 0, 98, 0, 262, 0, 196, 0, 262, 0, 262, 0, 392, 0, 466, 0, 587, 0, 196, 0, 262, 0, 262, 0, 117, 0, 392, 0, 466, 0, 587, 0, 196, 0, 147, 0, 196, 0, 392, 0, 587, 0, 117
};

int defaultDurations[] = {
  176, 102, 8, 2, 9, 2, 34, 1, 112, 9, 110, 44, 92, 186, 8, 2, 9, 3, 131, 182, 74, 204, 11, 2, 5, 2, 33, 2, 40, 84, 118, 7, 20, 6, 123, 5, 20, 84, 41, 11, 1, 7, 2, 33, 98, 12, 142, 20, 12, 84, 195, 8, 7, 39, 1, 64, 57, 143, 10, 151, 127, 8, 7, 39, 283, 92, 194, 7, 3, 36, 2, 58, 63, 110, 24, 16, 7, 116, 3, 27, 10, 122, 7, 3, 37, 99, 20, 134, 19, 8, 219, 59, 8, 7, 37, 2, 112, 10, 133, 21, 112, 167, 8, 7, 104, 214, 97, 181, 7, 3, 11, 2, 119, 40, 118, 8, 20, 6, 123, 1, 23, 45, 80, 7, 3, 9, 2, 33, 99, 23, 130, 20, 8, 96, 183, 10, 2, 7, 3, 32, 5, 92, 30, 146, 6, 84, 195, 8, 10, 36, 5, 57, 217, 246, 40, 7, 3, 37, 126, 116, 18, 16, 34, 91, 20, 141, 7, 3, 36, 100, 20, 137, 13, 12, 219, 59, 8, 7, 37, 2, 112, 9, 117, 36, 105, 173, 8, 7, 114, 204, 85, 194, 7, 3, 5, 2, 36, 1, 47, 75, 118, 8, 23, 6, 118, 13, 16, 102, 24, 7, 3, 5, 2, 36, 100, 20, 133, 20, 8, 84, 194, 8, 2, 10, 1, 34, 1, 69, 56, 146, 7, 88, 186, 10, 2, 8, 2, 32, 1, 39, 241, 82, 201, 8, 7, 37, 2, 56, 66, 112, 9, 23, 9, 117, 27, 61, 69, 8, 7, 37, 249, 23, 8, 117, 161, 8, 7, 39, 1, 89, 4, 23, 10, 2, 142, 4, 105, 17, 20, 134, 16, 8, 31, 3, 73, 204, 89, 197, 7, 3, 36, 2, 41, 54, 19, 8, 151, 5, 116, 3, 27, 41, 84, 23, 31, 127, 133, 20, 107, 188, 36, 5, 80, 10, 20, 154, 7, 92, 29, 12, 142, 23, 31, 4, 96, 179, 239, 47, 7, 3, 36, 99, 20, 8, 151, 6, 116, 15, 16, 75, 50, 12, 16, 27, 130, 99, 50, 88, 190, 10, 2, 8, 34, 5, 73, 18, 20, 4, 154, 6, 85, 37, 20, 142, 16, 27, 4, 29, 246, 246, 44, 7, 3, 37, 127, 151, 6, 58, 60, 20, 8, 77, 49, 23, 31, 5, 47, 71, 109, 44, 163, 115, 8, 7, 37, 2, 184, 63, 23, 287, 8, 7, 37, 2, 60, 61, 116, 37, 74, 79, 110, 11, 8, 7, 39, 3, 56, 221, 89, 63, 116, 13, 7, 3, 36, 2, 41, 238, 242, 37, 8, 7, 37, 2, 92, 6, 20, 9, 117, 4, 20, 283, 8, 7, 37, 2, 87, 8, 20, 134, 20, 8, 164, 115, 8, 7, 37, 2, 97, 25, 219, 90, 117, 5, 8, 2, 9, 3, 32, 1, 129, 149, 117, 161, 13, 6, 1, 34, 1, 92, 6, 12, 21, 112, 5, 20, 133, 20, 130, 10, 1, 6, 1, 34, 1, 82, 17, 16, 133, 20, 8, 138, 141, 10, 2, 6, 1, 34, 1, 97, 29, 171, 134, 107, 19, 8, 7, 37, 2, 86
};

#define DEFAULT_NUM_NOTES (sizeof(defaultMelody) / sizeof(int))

// Melody storage (dynamically loaded via BLE)
uint16_t melody[MAX_NOTES];
uint16_t durations[MAX_NOTES];
volatile int numNotes = 0;
volatile int writeIndex = 0;
volatile int playIndex = 0;  // Current position in melody

// State
volatile bool receiving = false;
volatile bool isPlaying = false;
volatile bool isPaused = true;  // Start paused, touch to play
bool lastTouchState = false;
BLECharacteristic* pControlChar = nullptr;

// Control commands
enum Command {
  CMD_START_UPLOAD = 0x01,  // Start receiving new melody
  CMD_END_UPLOAD = 0x02,    // Finished uploading
  CMD_PLAY = 0x03,          // Start playback
  CMD_STOP = 0x04,          // Stop playback
  CMD_STATUS = 0x05         // Request status
};

// Forward declarations
void setLedPause();
void setLedPlaying(uint16_t freq);
void bootSequence();

// BLE connection state
volatile int connectedClients = 0;

void loadDefaultMelody() {
  numNotes = DEFAULT_NUM_NOTES;
  for (int i = 0; i < numNotes; i++) {
    melody[i] = defaultMelody[i];
    durations[i] = defaultDurations[i];
  }
  playIndex = 0;
  Serial.printf("Loaded default melody: %d notes\n", numNotes);
}

class DataCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();

    if (!receiving || len < 4) return;

    // Parse frames (4 bytes each: uint16 freq + uint16 dur)
    for (size_t i = 0; i + 3 < len && writeIndex < MAX_NOTES; i += 4) {
      uint16_t freq = data[i] | (data[i + 1] << 8);
      uint16_t dur = data[i + 2] | (data[i + 3] << 8);

      melody[writeIndex] = freq;
      durations[writeIndex] = dur;
      writeIndex++;
    }

    // Blink blue LED to show upload activity
    analogWrite(LED_B_PIN, 255 );
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getLength();
    if (len < 1) return;

    uint8_t cmd = data[0];

    switch (cmd) {
      case CMD_START_UPLOAD:
        // Stop playback, reset and prepare for new melody
        isPlaying = false;
        isPaused = true;
        receiving = true;
        writeIndex = 0;
        playIndex = 0;  // Reset position for new melody
        noTone(BUZZER_PIN);
        Serial.println("Starting upload...");
        break;

      case CMD_END_UPLOAD:
        // Finalize upload
        receiving = false;
        numNotes = writeIndex;
        playIndex = 0;  // Start from beginning
        Serial.printf("Upload complete: %d notes\n", numNotes);

        // Send confirmation
        if (pControlChar) {
          uint8_t response[3] = {CMD_END_UPLOAD, (uint8_t)(numNotes & 0xFF), (uint8_t)(numNotes >> 8)};
          pControlChar->setValue(response, 3);
          pControlChar->notify();
        }
        break;

      case CMD_PLAY:
        if (numNotes > 0) {
          isPaused = false;
          isPlaying = true;
          Serial.println("Playing...");
        }
        break;

      case CMD_STOP:
        isPaused = true;
        noTone(BUZZER_PIN);
        setLedPause();
        Serial.println("Paused");
        break;

      case CMD_STATUS:
        if (pControlChar) {
          uint8_t status[7] = {
            CMD_STATUS,
            (uint8_t)(numNotes & 0xFF),
            (uint8_t)(numNotes >> 8),
            (uint8_t)(playIndex & 0xFF),
            (uint8_t)(playIndex >> 8),
            isPaused ? 0 : 1,
            receiving ? 1 : 0
          };
          pControlChar->setValue(status, 7);
          pControlChar->notify();
        }
        break;
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    connectedClients++;
    Serial.printf("Client connected (%d total)\n", connectedClients);
    setLedPause();  // Violet quand connecté
    // Continue advertising to allow more connections
    BLEDevice::startAdvertising();
  }

  void onDisconnect(BLEServer* pServer) {
    connectedClients--;
    if (connectedClients < 0) connectedClients = 0;
    Serial.printf("Client disconnected (%d remaining)\n", connectedClients);
    if (connectedClients == 0) {
      receiving = false;
    }
    setLedPause();  // Bleu si plus personne, violet sinon
    // Always keep advertising
    BLEDevice::startAdvertising();
  }
};

// Old school PC boot sequence simulation
void bootSequence() {
  Serial.println("POST starting...");

  tone(BUZZER_PIN, 400);
  delay(10);
  noTone(BUZZER_PIN);
  delay(400);
  
  // 1. Memory count - 640 rapid ticks (like counting 640KB)
  Serial.println("Memory check... ");
  analogWrite(LED_B_PIN, 0);
  analogWrite(LED_G_PIN, 255);
  for (int i = 0; i < 128; i++) {  
    tone(BUZZER_PIN, 800  );  // Fixed frequency tick
    analogWrite(LED_R_PIN, i * 2);  // Green to red progression
    analogWrite(LED_G_PIN, 256 - i * 2);  // Green to red progression
    delay(2);
    noTone(BUZZER_PIN);
    delay(10+random(10));
  }
  delay(900);

  // 2. Floppy A: check
  Serial.println("Floppy A: check...");
  analogWrite(LED_R_PIN, 255);
  for (int i = 0; i < 20; i++) {
    analogWrite(LED_G_PIN, 100 + random(50));
    tone(BUZZER_PIN, 130 + random(80));
    delay(10+random(10));
    noTone(BUZZER_PIN);
    delay(10+random(20));
  }
  for (int i = 0; i < 10; i++) {
    analogWrite(LED_G_PIN, 100 + random(50));
    tone(BUZZER_PIN, 60 + random(40));
    delay(10+random(10));
    noTone(BUZZER_PIN);
    delay(10+random(20));
  }
  analogWrite(LED_R_PIN, 0);
  analogWrite(LED_G_PIN, 0);
  delay(800);

  // 3. Floppy B: check
  Serial.println("Floppy B: check...");
  analogWrite(LED_R_PIN, 255);
  for (int i = 0; i < 20; i++) {
    analogWrite(LED_G_PIN, 100 + random(50));
    tone(BUZZER_PIN, 80 + random(80));
    delay(10+random(10));
    noTone(BUZZER_PIN);
    delay(10+random(20));
  }
  for (int i = 0; i < 10; i++) {
    analogWrite(LED_G_PIN, 100 + random(50));
    tone(BUZZER_PIN, 40 + random(40));
    delay(10+random(10));
    noTone(BUZZER_PIN);
    delay(10+random(20));
  }
  analogWrite(LED_R_PIN, 0);
  analogWrite(LED_G_PIN, 0);
  delay(900);

  // 4. Final POST beep (the classic single beep = all OK)
  Serial.println("POST complete!");
  analogWrite(LED_R_PIN, 0);
  analogWrite(LED_G_PIN, 255);
  analogWrite(LED_B_PIN, 0);
  tone(BUZZER_PIN, 800);
  delay(250);
  noTone(BUZZER_PIN);
  delay(100);

  // Reset LED to idle blue
  analogWrite(LED_G_PIN, 0);
  analogWrite(LED_B_PIN, 255);
  analogWrite(LED_R_PIN, 0);
}

void setup() {
  Serial.begin(115200);
  Serial.println("BLE Buzzer starting...");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);

  // Boot sequence (like an old PC)
  bootSequence();

  // Load default melody
  loadDefaultMelody();

  // Initialize BLE
  BLEDevice::init("PCSpeakr");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Data characteristic (write frames here)
  BLECharacteristic* pDataChar = pService->createCharacteristic(
    CHAR_DATA_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pDataChar->setCallbacks(new DataCallbacks());

  // Control characteristic (commands + notifications)
  pControlChar = pService->createCharacteristic(
    CHAR_CONTROL_UUID,
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pControlChar->setCallbacks(new ControlCallbacks());
  pControlChar->addDescriptor(new BLE2902());

  // Start service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("BLE ready, waiting for connection...");
  Serial.println("Touch pin 4 to play/pause");
}

void checkTouch() {
  bool touching = (touchRead(TOUCH_PIN) < TOUCH_THRESHOLD);

  // Detect rising edge (finger just touched)
  if (touching && !lastTouchState) {
    isPaused = !isPaused;
    if (isPaused) {
      noTone(BUZZER_PIN);
      setLedPause();
      Serial.println("Paused");
    } else {
      Serial.printf("Playing from note %d/%d\n", playIndex, numNotes);
    }
  }

  lastTouchState = touching;
}

// Fonction pour mettre la LED en mode "playing" (vert-rouge selon fréquence)
void setLedPlaying(uint16_t freq) {
  if (freq == 0) {
    // Silence = LED éteinte pendant la lecture
    analogWrite(LED_R_PIN, 0);
    analogWrite(LED_G_PIN, 0);
    analogWrite(LED_B_PIN, 0);
    return;
  }

  // Gradient simple vert → rouge selon fréquence
  int normalized = constrain(freq, 50, 800);
  int red = map(normalized, 50, 800, 0, 255);
  int green = 255 - red;

  analogWrite(LED_B_PIN, 0);
  analogWrite(LED_R_PIN, red);
  analogWrite(LED_G_PIN, green);
}

// Fonction pour mettre la LED en mode pause (bleu ou violet si connecté)
void setLedPause() {
  analogWrite(LED_G_PIN, 0);
  if (connectedClients > 0) {
    // Violet quand connecté
    analogWrite(LED_R_PIN, 255);
    analogWrite(LED_B_PIN, 255);
  } else {
    // Bleu quand déconnecté
    analogWrite(LED_R_PIN, 0);
    analogWrite(LED_B_PIN, 255);
  }
}

void playOneNote() {
  if (isPaused || numNotes == 0) {
    return;
  }

  uint16_t freq = melody[playIndex];
  uint16_t dur = durations[playIndex];

  // LED couleur selon fréquence
  setLedPlaying(freq);

  if (freq > 0) {
    tone(BUZZER_PIN, freq);
  } else {
    noTone(BUZZER_PIN);
  }

  // Wait for note duration, but check touch periodically
  unsigned long startTime = millis();
  while (millis() - startTime < dur) {
    checkTouch();
    if (isPaused) {
      noTone(BUZZER_PIN);
      return;  // Exit early if paused
    }
    delay(5);
  }

  // Advance to next note
  playIndex++;
  if (playIndex >= numNotes) {
    playIndex = 0;  // Loop back to start
    Serial.println("Melody finished, looping...");
  }
}

void loop() {
  checkTouch();
  playOneNote();

  // Small delay when paused to reduce CPU usage
  if (isPaused) {
    delay(50);
  }
}
