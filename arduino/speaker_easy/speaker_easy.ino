//test on com7 with powershell
// $port = New-Object System.IO.Ports.SerialPort COM7,115200,None,8,one
// $port.Open()
// # Envoi du paquet : 0x06 (Cmd), 0xB8, 0x01 (440Hz), 0x00, 0x00 (Durée)
// $packet = [byte[]] @(0x06, 0xB8, 0x01, 0x00, 0x00)
// $port.Write($packet, 0, $packet.Count)
// $port.Close()

#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

// Broche de la LED (GPIO 23)
const int ledRPin = 23;
const int ledGPin = 22;
const int ledBPin = 1;
const int buzzerPin = 16;

// Control commands
enum Command {
  CMD_START_UPLOAD = 0x01,  // Start receiving new melody
  CMD_END_UPLOAD = 0x02,    // Finished uploading
  CMD_PLAY = 0x03,          // Start playback
  CMD_STOP = 0x04,          // Stop playback
  CMD_STATUS = 0x05,        // Request status
  CMD_STREAM_NOTE = 0x06    // Stream a note in real-time
};

// État de la LED et watchdog
unsigned long lastNoteTime = 0;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("spkr-ez", false, true);
  pinMode(ledRPin, OUTPUT);
  pinMode(ledGPin, OUTPUT);
  pinMode(ledBPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  // Petit chenillard au démarrage pour vérifier la LED
  digitalWrite(ledGPin, HIGH); delay(100);
  digitalWrite(ledBPin, HIGH); delay(100);
  digitalWrite(ledGPin, LOW); delay(100);
  digitalWrite(ledBPin, LOW); delay(100);
  digitalWrite(ledRPin, HIGH); delay(100);
  digitalWrite(ledRPin, LOW); delay(100);

}

void handleCommand(Stream& input, const char* source) {
  byte cmd = input.read();
  Serial.print("[");
  Serial.print(source);
  Serial.print("] CMD: 0x");
  Serial.print(cmd, HEX);
  Serial.print(" | available: ");
  Serial.println(input.available());

  switch (cmd) {
    case CMD_STREAM_NOTE:
      if (input.available() >= 4) {
        uint8_t buffer[4];
        input.readBytes(buffer, 4);
        uint16_t freq = buffer[0] | (buffer[1] << 8);
        uint16_t dur = buffer[2] | (buffer[3] << 8);

        Serial.print("  -> STREAM_NOTE freq=");
        Serial.print(freq);
        Serial.print(" dur=");
        Serial.println(dur);
        
        // Gradient simple vert → rouge selon fréquence
        int normalized = constrain(freq, 50, 800);
        int red = map(normalized, 50, 800, 0, 255);
        int green = 255 - red;
        analogWrite(ledBPin, 0);
        analogWrite(ledRPin, red);
        analogWrite(ledGPin, green);
        
        if (freq > 0) {
          lastNoteTime = millis();
          tone(buzzerPin, freq);
        } else {
          noTone(buzzerPin);
        }
      } else {
        Serial.print("  -> ERREUR: pas assez de bytes (");
        Serial.print(input.available());
        Serial.println("/4)");
      }
      break;

    case CMD_START_UPLOAD:
      Serial.println("  -> START_UPLOAD");
      break;

    case CMD_END_UPLOAD:
      Serial.println("  -> END_UPLOAD");
      break;

    case CMD_PLAY:
      Serial.println("  -> PLAY");
      break;

    case CMD_STOP:
      digitalWrite(ledRPin, LOW);
      digitalWrite(ledGPin, LOW);
      digitalWrite(ledBPin, LOW);
      lastNoteTime = 0;
      Serial.println("  -> STOP");
      break;

    case CMD_STATUS:
      Serial.println("  -> STATUS");
      break;

    default:
      Serial.print("  -> UNKNOWN CMD: 0x");
      Serial.println(cmd, HEX);
      break;
  }
}

void loop() {
  if (Serial.available() > 0) {
    handleCommand(Serial, "USB");
  }
  if (SerialBT.available() > 0) {
    handleCommand(SerialBT, "BT");
  }

  // Watchdog de sécurité (5s)
  if (lastNoteTime > 0 && (millis() - lastNoteTime > 5000)) {
    digitalWrite(ledRPin, LOW);
    digitalWrite(ledGPin, LOW);
    digitalWrite(ledBPin, LOW);
    lastNoteTime = 0;
  }
}