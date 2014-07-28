/**************************************************************************/
/*!
 @file     NFC_Door
 @author   Forbes Holden
 
 @brief  This program will attempt to connect to an ISO14443A / 13.56 MHz
         card or tag and retrieve the UID. This is then compared with 
         the internal database to determine if the door should be opened.
 
         Cards can be added and removed from the system using a hard coded
         master card. The master will also clear the system of all saved
         cards when swiped twice consecutively.
 
         Note that you need the baud rate to be 115200 because we need to
         print out the data and read from the card at the same time!
 */
/**************************************************************************/
#include <Wire.h>
#include <Adafruit_NFCShield_I2C.h>   // https://github.com/adafruit/Adafruit_NFCShield_I2C
#include <EEPROM.h>

#define IRQ   (2)
#define RESET (3)   // Not connected by default on the NFC Shield

Adafruit_NFCShield_I2C _nfc(IRQ, RESET);

const int     _motorDirectionPin1 = 6; // Motor direction pin 1
const int     _motorDirectionPin2 = 7; // Motor direction pin 2
const int     _motorEnablePin = 8;     // Motor enabling pin
const int     _blueLEDPin = 9;         // Blue LED pin
const int     _redLEDPin = 10;         // Red LED pin
const int     _greenLEDPin = 11;       // Green LED pin

uint8_t       _scannedUIDBuffer[7];    // Buffer for UID bytes from NFC card
uint8_t       _uidLength;              // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
uint8_t       _entryCount = 0;         // Variable to store value from first byte of EEPROM, which is a count for number of saved cards
uint8_t       _readUIDBuffer[7];       // Buffer for UID bytes from EEPROM
boolean       _adminMode = false;      // Variable to turn admin mode on or off

const uint8_t _masterUIDEntry[7] =     // Hard coded master card UID entry
{
  240, 200, 160, 120, 80, 40, 0         // Put your master card UID here
};

const uint8_t _nullUIDEntry[7] =             // Hard coded null UID entry
{
  255, 255, 255, 255, 255, 255, 255
};

/**************************************************************************/
/*! 
 @brief  Inital Arduino setup function
 */
/**************************************************************************/
void setup()
{
  Serial.begin(115200);   // Initialise serial communication

  // Initialise pins
  pinMode(_motorDirectionPin1, OUTPUT);
  pinMode(_motorDirectionPin2, OUTPUT);
  pinMode(_motorEnablePin, OUTPUT);
  pinMode(_greenLEDPin, OUTPUT);
  pinMode(_redLEDPin, OUTPUT);
  pinMode(_blueLEDPin, OUTPUT);

  // Initialise arrays
  for (int i = 0; i < 7; i++)
  {
    _scannedUIDBuffer[i] = 0;
    _readUIDBuffer[i] = 0;
  }

  // Check for a PN53X Board, then enable it and print firmware it's information
  _nfc.begin();

  uint32_t versionData = _nfc.getFirmwareVersion();

  if (!versionData)
  {
    Serial.print("Didn't find PN53x board");
    while (1); // Halt
  }

  Serial.print("Found chip PN5");
  Serial.print((versionData>>24) & 0xFF, HEX);
  Serial.print(" - Firmware ver. ");
  Serial.print((versionData>>16) & 0xFF, DEC);
  Serial.print('.');
  Serial.print((versionData>>8) & 0xFF, DEC);
  Serial.print(" - ");

  _nfc.SAMConfig();   // Configure board to read RFID tags
  Serial.println("Waiting for an ISO14443A / 13.56 MHz card");
}

/**************************************************************************/
/*! 
 @brief  Main Arduino loop function
 */
/**************************************************************************/
void loop()
{
  if (!_adminMode)
  {
    setLEDMode(2);   // Blue LED
  }
  else
  {
    setLEDMode(4);   // Yellow LED
  }

  boolean scanSuccess = false;

  scanSuccess = _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &_scannedUIDBuffer[0], &_uidLength);   // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found the scanned UID buffer will be populated with the UID

  if (scanSuccess)
  { 
    String scannedUIDString = "";

    for (int i = 6; i >= 0; i--)   // Concat the scanned UID Buffer into a string for display
    {
      scannedUIDString += _scannedUIDBuffer[i];
    }

    Serial.print("Card detected #: ");
    Serial.print(scannedUIDString);   // Print out the UID string
    Serial.print(" - ");

    if (checkUIDsMatch((uint8_t*)&_scannedUIDBuffer, (uint8_t*)&_masterUIDEntry))   // Master card detected
    {
      if (!_adminMode)   // Turn on admin mode and pause to avoid scanning the admin card again
      {
        Serial.println("Master detected. Entering admin mode.");
        _adminMode = true;
        setLEDMode(0);   // Turn off LED
        delay(1250);
      }
      else   // Set first byte to 0 for the entry count and set the standard null value to each remaining byte of the EEPROM
      {
        Serial.println("Master detected. Admin mode already enabled, clearing database.");
        setLEDMode(7);   // White LED

        eepromSafeWrite(0, 0);

        for (int i = 1; i < 1024; i++)
        {
          eepromSafeWrite(i, _nullUIDEntry[i]);
        }

        delay(2000);
        _adminMode = false;
      }
    }
    else   // Detected card is not the master
    {
      _entryCount = EEPROM.read(0);   // Check first byte of EEPROM for entry count

      if (_entryCount > 0)   // Ensure there is something in the EEPROM
      {
        int currentReadAddress = 1;   // Address to start reading EEPROM from, first (0) is reserved for the count
        boolean entryFound = false;   // Variable to store result of checks

        for (int currentEntry = 0; currentEntry < _entryCount; currentEntry++)
        {
          uint8_t* entryArrayPointer;
          entryArrayPointer = (uint8_t*)&_readUIDBuffer;

          for (int i = 0; i < 7; i++)   // Read the current entry into read UID buffer
          {
            entryArrayPointer[i] = EEPROM.read(currentReadAddress +i);
          }

          if (checkUIDsMatch((uint8_t*)&_scannedUIDBuffer, (uint8_t*)&_readUIDBuffer))   // Check the scannned entry against the detected card's UID
          {
            if (!_adminMode)
            {
              Serial.println("Found in database. Unlocking door.");
              unlockDoor();
            }
            else   // Admin mode is enabled, entry was found. Overwrite with null entry
            {
              for (int currentWriteAddress = currentReadAddress; currentWriteAddress < currentReadAddress +7; currentWriteAddress++)
              {
                eepromSafeWrite(currentWriteAddress, _nullUIDEntry[0]);
              }

              Serial.println("Found in database. Removed entry.");
              setLEDMode(5);   // Magenta LED
              delay(2000);
              _adminMode = false;
            }

            entryFound = true;
            break;   // Entry was found, exit this for loop
          }

          currentReadAddress += 7;
        }

        if (!entryFound)
        {
          if (!_adminMode)
          {
            Serial.println("Not found in database. Access denied!");
            setLEDMode(1);   // Red LED
            delay(1250);
          }
          else
          {
            eepromWriteScannedUIDBuffer();
          }
        }
      }
      else
      {
        if (!_adminMode)
        {
          Serial.println("Database empty. Access denied!");
          setLEDMode(1);   // Red LED
          delay(1250);
        }
        else
        {
          eepromWriteScannedUIDBuffer();
        }
      }
    }
  }
}

/**************************************************************************/
/*! 
 @brief  Set LED colour
 
 @param  pColourMode    An integer value representing the chosen LED colour
 */
/**************************************************************************/
void setLEDMode(int pColourMode)
{
  const int ledOff = 0;
  const int ledLow = 100;
  const int ledMedium = 130;
  const int ledHigh = 255;

  switch (pColourMode)
  {
  case 1:   // Red
    analogWrite(_greenLEDPin, ledOff); 
    analogWrite(_redLEDPin, ledHigh);
    analogWrite(_blueLEDPin, ledOff);   
    break;

  case 2:   // Blue
    analogWrite(_greenLEDPin, ledOff); 
    analogWrite(_redLEDPin, ledOff);
    analogWrite(_blueLEDPin, ledHigh);   
    break;

  case 3:   // Green
    analogWrite(_greenLEDPin, ledHigh); 
    analogWrite(_redLEDPin, ledOff);
    analogWrite(_blueLEDPin, ledOff);
    break;

  case 4:   // Yellow
    analogWrite(_greenLEDPin, ledHigh);
    analogWrite(_redLEDPin, ledMedium);
    analogWrite(_blueLEDPin, ledOff);
    break;

  case 5:   // Magenta
    analogWrite(_greenLEDPin, ledOff);
    analogWrite(_redLEDPin, ledMedium);
    analogWrite(_blueLEDPin, ledHigh);
    break;

  case 6:   // Cyan
    analogWrite(_greenLEDPin, ledHigh);
    analogWrite(_redLEDPin, ledOff);
    analogWrite(_blueLEDPin, ledMedium);
    break;

  case 7:   // White
    analogWrite(_greenLEDPin, ledHigh);
    analogWrite(_redLEDPin, ledLow);
    analogWrite(_blueLEDPin, ledMedium);
    break;

  case 0:   // Off
  default:
    analogWrite(_greenLEDPin, ledOff);
    analogWrite(_redLEDPin, ledOff);
    analogWrite(_blueLEDPin, ledOff);
    break;
  }
}

/**************************************************************************/
/*! 
 @brief  Check UIDs match
 
 @param  pUIDArrayPointer1  Pointer to the UID to be checked
 @param  pUIDArrayPointer2  Pointer to the UID for comparison
 */
/**************************************************************************/
static boolean checkUIDsMatch(const uint8_t* pUIDArrayPointer1, const uint8_t* pUIDArrayPointer2)
{
  boolean uidMatches = true;

  for (int i = 0; i < 7; i++)   // Compare the scanned and read buffers
  {    
    if (pUIDArrayPointer1[i] != pUIDArrayPointer2[i])
    {
      uidMatches = false;
    }
  }

  return uidMatches;
}

/**************************************************************************/
/*! 
 @brief  Prevent unnecessary writes and reduce EEPROM wear
 
 @param  pAddress    The EEPROM address to be writen on
 @param  pWriteByte  The byte to be written on this address
 */
/**************************************************************************/
static void eepromSafeWrite(const unsigned int pAddress, const byte pWriteByte)
{
  if (EEPROM.read(pAddress) != pWriteByte)
  {
    EEPROM.write(pAddress, pWriteByte);
  }
}

/**************************************************************************/
/*! 
 @brief  Allow door to be opened
 
 @memo   Currently this is in its own method for easy access
         once my motor circuit is completed seperately
         as changes are likely to be made
 */
/**************************************************************************/
static void unlockDoor()
{
  // Unlock the door
  setLEDMode(3);   // Green LED
  digitalWrite(_motorDirectionPin1, HIGH);
  digitalWrite(_motorDirectionPin2, LOW);
  analogWrite(_motorEnablePin, 1);
  delay(1250);
  digitalWrite(_motorDirectionPin1, LOW);
  digitalWrite(_motorDirectionPin2, LOW);
  analogWrite(_motorEnablePin, 0);

  delay(500);   // Pause to allow door to be opened

  // Lock the door
  setLEDMode(1);   // Red LED
  digitalWrite(_motorDirectionPin1, LOW);
  digitalWrite(_motorDirectionPin2, HIGH);
  analogWrite(_motorEnablePin, 1);
  delay(1250);
  digitalWrite(_motorDirectionPin1, LOW);
  digitalWrite(_motorDirectionPin2, LOW);
  analogWrite(_motorEnablePin, 0);
}

/**************************************************************************/
/*! 
 @brief  Save the CardId to EEPROM memory
 */
/**************************************************************************/
void eepromWriteScannedUIDBuffer()
{
  boolean recycledEntry = false;

  if (_entryCount > 0)   // If entry count is > 0, it could be false due to removed entries. Check for recyclable space.
  {
    int currentReadAddress = 1;

    for (int currentEntry = 1; currentEntry <= _entryCount; currentEntry++)   // The number of entries to check is taken from the entry count
    {
      uint8_t* entryArrayPointer;
      entryArrayPointer = (uint8_t*)&_readUIDBuffer;

      for (int i = 0; i < 7; i++)   // Read the current entry into read UID buffer
      {
        entryArrayPointer[i] = EEPROM.read(currentReadAddress +i);
      }

      if (checkUIDsMatch((uint8_t*)&_readUIDBuffer, (uint8_t*)&_nullUIDEntry))   // Check if entry entry is nulled, if so recycle the space and do not update the entry count
      {
        for (int i = 0; i < 7; i++)   // Write to the current entry address of the EEPROM. Each card id takes 7 bytes but we also skip the count byte
        {
          eepromSafeWrite((currentEntry * 7) + 1 + i, _scannedUIDBuffer[i]);
        }

        Serial.println("Not found in database. Card saved!");
        recycledEntry = true;
        setLEDMode(6);   // Cyan LED
        delay(2000);
        break;
      }

      currentReadAddress += 7;
    }
  }

  if (!recycledEntry) // If no space can be recycled
  {
    if (_entryCount < 145)   // If there is free space, write the entry and update entry count
    {
      for (int i = 0; i < 7; i++)   // Write to the current entry address of the EEPROM. Each card id takes 7 bytes but we also skip the count byte
      {
        eepromSafeWrite((_entryCount * 7) + 1 + i, _scannedUIDBuffer[i]);
      }

      EEPROM.write(0, _entryCount + 1);   // Update entry count
      Serial.println("Not found in database. Card saved!");
      setLEDMode(6);   // Cyan LED
      delay(2000);
    }
    else if (_entryCount >= 145)  // Not enough space, return error
    {
      Serial.println("Out of space! All entries currently in use.");
      setLEDMode(1);   // Red LED
      delay(1250);
    }
  }

  _adminMode = false;   // Turn off admin mode
}
