/* Original link to this script: https://github.com/eric-b/reset-bob-cassette
 * 
 * Reset a Bob cassette (Pop / Rock)
 * Based on https://github.com/dekuNukem/bob_cassette_rewinder.
 * 
 * Bob cassette contains a 24C02 EEPROM wired on I2C bus.
 * 
 * Wiring:
 * Arduino  / USB A
 * A4 (SDA) / D- 
 * A5 (SCL) / D+
 * 3.3V     / +
 * Gnd      / -
 * 
 *  Cassette connector: 
 *  Isolate/cover (with a piece of paper) connector side opposite to cassette labels and
 *  connect USB A female to connector on cassette side with labels.
 * 
 * Blinking status:
 * - Alternate 0.5 sec: no cassette found / cassette not recognized.
 * - Continuous 10 sec: cassette found - reset successful or nothing to do.
 * - Alternate 100ms for 5 sec: cassette found - failed to reset cassette.
 * 
 * Resources:
 * https://github.com/dekuNukem/bob_cassette_rewinder
 * https://learn.sparkfun.com/tutorials/reading-and-writing-serial-eeproms/all
 * https://create.arduino.cc/projecthub/gatoninja236/how-to-use-i2c-eeprom-30767d
 * https://gammon.com.au/i2c
 */

#include <Wire.h>

#define ADDR_I2C_DEVICE_SELECT 0b000
#define ADDR_I2C_DEVICE (0b1010 << 3) + ADDR_I2C_DEVICE_SELECT

/*
 * I2C EEPROM addressing:
 * 1  2  3  4  5  6  7  (MSB)
 * 1  0  1  0  A2 A1 A0
 * 
 * (A0-A2 known as Device Address Inputs,  
 * Address Select or Device Address Bits for 
 * serial EEPROM chips).
 * 
 * With ADDR_I2C_DEVICE_SELECT=0b000, 
 * then ADDR_I2C_DEVICE=0x50 on 7 bits 
 * (would be 0xA0 on 8 bits with 8th bit=0 or 
 * 0xA1 with 8th bit=1)
 * 
 * With ADDR_I2C_DEVICE_SELECT=0b001, 
 * then ADDR_I2C_DEVICE=0x51 on 7 bits
 * 
 * and so on... 
 * (allows 8 EEPROM chips on of same kind on a single I2C bus)
 * 
 * Here, ADDR_I2C_DEVICE_SELECT=0 because we know it
 * (depends how the eeprom chip is wired). 0 is typical 
 * if there is a single EEPROM wired on I2C.
 */

const byte CASSETTE_REMAINING_COUNT_BYTE_ADDR = 0xA1;
const byte CASSETTE_CATEGORY_BYTE_ADDR = 0x90;
const byte CASSETTE_POP_FULL_COUNT_VALUE = 0x4E; // = 30 (wash left XOR 0x50)
const byte CASSETTE_ROCK_FULL_COUNT_VALUE = 0x51; // = 1 (wash left XOR 0x50)

const byte TRANSMISSION_SUCCESS_CODE = 0;
const byte READ_BYTE_ERROR_VALUE = 0xFF;

enum cassette_category {
  undefined,
  unknown,
  pop,
  rock
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600); // start serial for debugging
  Wire.begin(); // join i2c bus (address optional for master)
  Serial.println("Program started");
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  cassette_category cassette = readCassetteKind();
  switch (cassette) {
  case pop:
    processPopRockCassette(CASSETTE_POP_FULL_COUNT_VALUE);
    break;
  case rock:
    processPopRockCassette(CASSETTE_ROCK_FULL_COUNT_VALUE);
    break;
  case unknown:
    blinkFast5Sec();
    break;
  default:
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    break;
  }

  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}

bool processPopRockCassette(byte maxWashLeftRawValue) {
  byte washLeftValue = readCurrentWashLeftRawValue();
  if (washLeftValue == READ_BYTE_ERROR_VALUE) {
    Serial.println("Failed to reset cassette.");
    blinkFast5Sec();
    return false;
  }
  
  if (isWashLeftLessThanMaxValue(washLeftValue, maxWashLeftRawValue)) {
    if (writeCassetteWashLeft(maxWashLeftRawValue)) {
      Serial.println("Cassette was reset successfully.");
      digitalWrite(LED_BUILTIN, HIGH);
      delay(10000);
    } else {
      Serial.println("Failed to reset cassette.");
      blinkFast5Sec();
    }
  } else {
    Serial.println("Nothing to do.");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(10000);
  }
}

bool isWashLeftLessThanMaxValue(byte washLeft, byte maxValue) {
  return (washLeft ^ 0x50) < (maxValue ^ 0x50);
}

cassette_category readCassetteKind() {
  int i;
  char buffer[10]; // length of 9 + 1 for NULL terminated string
  bool hasReadCassetteType = readI2CBytes(CASSETTE_CATEGORY_BYTE_ADDR, buffer, sizeof buffer - 1);
  if (hasReadCassetteType) {
    buffer[9] = 0; // NULL terminated string required for char array to string
    if (strcmp((char * ) buffer, "Classique") == 0) {
      Serial.println("Pop Cassette recognized");
      return pop;
    } else if (strcmp((char * ) buffer, "Entretien") == 0) {
      Serial.println("Rock Cassette recognized");
      return rock;
    } else {
      String value = buffer;
      Serial.println("Cassette not recognized: '" + value + "'");
      for (i = 0; i < sizeof buffer - 1; i++) {
        Serial.print(buffer[i], HEX);
      }
      Serial.println();
      return unknown;
    }
  }

  Serial.println("Cannot read cassette.");
  return undefined;
}

byte readCurrentWashLeftRawValue() {
  Serial.print("Reading wash left...");
  byte rawValue = readI2CByte(CASSETTE_REMAINING_COUNT_BYTE_ADDR);
  Serial.println(rawValue ^ 0x50);
  return rawValue;
}

bool writeCassetteWashLeft(uint8_t washLeftRawValue) {
  return writeI2CByte(CASSETTE_REMAINING_COUNT_BYTE_ADDR, washLeftRawValue);
}

bool writeI2CByte(byte data_addr, byte data_value) {
  byte statusCode;
  Wire.beginTransmission(ADDR_I2C_DEVICE);
  Wire.write(data_addr);
  Wire.write(data_value);
  statusCode = Wire.endTransmission();
  if (statusCode != TRANSMISSION_SUCCESS_CODE) {
    return false;
  }

  // wait for write to finish by sending address again
  //  ... give up after 100 attempts (1/10 of a second)
  byte counter;
  for (counter = 0; counter < 100; counter++) {
    delayMicroseconds(300);
    Wire.beginTransmission(ADDR_I2C_DEVICE);
    Wire.write(data_addr);
    statusCode = Wire.endTransmission();
    if (statusCode == TRANSMISSION_SUCCESS_CODE)
      return true;
  }

  return false;
}

// Read one byte of data at specified address.
// Returns 0xFF in case of read error.
byte readI2CByte(byte data_addr) {
  const int data_length = 1;
  byte data = READ_BYTE_ERROR_VALUE;
  Wire.beginTransmission(ADDR_I2C_DEVICE);
  Wire.write(data_addr);
  if (Wire.endTransmission() != TRANSMISSION_SUCCESS_CODE) {
    return data;
  }

  Wire.requestFrom((uint8_t) ADDR_I2C_DEVICE, data_length);

  if (waitForI2CBufferAvailable(1)) {
    data = Wire.read();
  }

  return data;
}

// read dataLength (max 32) bytes from device, returns false on error.
bool readI2CBytes(byte data_addr, char * data, int dataLength) {
  byte statusCode;
  byte counter;

  if (dataLength > BUFFER_LENGTH) // 32 (in Wire.h)
    return false;

  Wire.beginTransmission(ADDR_I2C_DEVICE);
  Wire.write(data_addr);
  statusCode = Wire.endTransmission();

  if (statusCode != TRANSMISSION_SUCCESS_CODE)
    return false;

  Wire.requestFrom((uint8_t) ADDR_I2C_DEVICE, dataLength);
  if (waitForI2CBufferAvailable(dataLength)) {
    for (counter = 0; counter < dataLength; counter++) {
      data[counter] = Wire.read();
    }
  }

  return true;
}

bool waitForI2CBufferAvailable(byte dataLength) {
  const byte waitTimeout = 100;
  byte elapsedTime = 0;
  while (Wire.available() < dataLength && elapsedTime < waitTimeout) {
    delay(1);
    elapsedTime = elapsedTime + 1;
  }
  return Wire.available() >= dataLength;
}

// 5sec blink fast for errors
void blinkFast5Sec() {
  byte counter;
  for (counter = 0; counter < 50; counter++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}
