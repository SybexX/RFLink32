#define AVANTEK_PLUGIN_ID 077
#define PLUGIN_DESC_077 "Avantek doorbells"

#ifdef PLUGIN_077
#include "../4_Display.h"
#include "../1_Radio.h"
#include "../7_Utils.h"

#define PLUGIN_077_ID "Avantek"
// #define PLUGIN_077_DEBUG

#define AVTK_PULSE_DURATION_MID_D 480
#define AVTK_PULSE_DURATION_MIN_D 380
#define AVTK_PULSE_DURATION_MAX_D 580

bool decode_manchester(uint8_t frame[], uint8_t expectedBitCount,
                       uint16_t const pulses[], const int pulsesCount,
                       int *pulseIndex, uint16_t shortPulseMinDuration,
                       uint16_t shortPulseMaxDuration,
                       uint16_t longPulseMinDuration,
                       uint16_t longPulseMaxDuration, uint8_t bitOffset,
                       bool lsb) {
  if (*pulseIndex + (expectedBitCount - 1) * 2 > pulsesCount) {
#ifdef MANCHESTER_DEBUG
    Serial.printf(
        F("MANCHESTER_DEBUG: Not enough pulses: *pulseIndex = %d - expectedBitCount = %d - pulsesCount = %d - min required pulses = %d\n"),
        *pulseIndex, expectedBitCount, pulsesCount,
        *pulseIndex + expectedBitCount * 2);
#endif
    return false;
  }

  // TODO we could add parameter "bitsPerByte"
  const uint8_t bitsPerByte = 8;
  const uint8_t endBitCount = expectedBitCount + bitOffset;

  for (uint8_t bitIndex = bitOffset; bitIndex < endBitCount; bitIndex++) {
    int currentFrameByteIndex = bitIndex / bitsPerByte;
    uint16_t bitDuration0 = pulses[*pulseIndex];
    uint16_t bitDuration1 = pulses[*pulseIndex + 1];

    // TODO we could add parameter of manchester/inversed manchester
    if (value_between(bitDuration0, shortPulseMinDuration,
                       shortPulseMaxDuration) &&
        value_between(bitDuration1, longPulseMinDuration,
                       longPulseMaxDuration)) {
      uint8_t offset = bitIndex % bitsPerByte;
      frame[currentFrameByteIndex] |=
          1 << (lsb ? offset : (bitsPerByte - 1 - offset));
    } else if (!value_between(bitDuration0, longPulseMinDuration,
                               longPulseMaxDuration) ||
               !value_between(bitDuration1, shortPulseMinDuration,
                               shortPulseMaxDuration)) {
#ifdef MANCHESTER_DEBUG
      Serial.printf(F("MANCHESTER_DEBUG: Invalid duration at pulse %d - bit %d: %d\n"),
             *pulseIndex, bitIndex,
             bitDuration0 * RFLink::Signal::RawSignal.Multiply);
#endif
      return false; // unexpected bit duration, invalid format
    }

    *pulseIndex += 2;
  }

  return true;
}

// TODO why can't  we use the function defined in 7_Utils?
inline bool value_between(uint16_t value, uint16_t min, uint16_t max) {
  return ((value > min) && (value < max));
}

inline bool isLowPulseIndex(const int pulseIndex) { return (pulseIndex % 2 == 1); }

uint8_t decode_bits(uint8_t frame[], const uint16_t *pulses,
                    const size_t pulsesCount, int *pulseIndex,
                    uint16_t pulseDuration, size_t bitsToRead) {
  size_t bitsRead = 0;

  for (size_t i = 0; *pulseIndex + i < pulsesCount && bitsRead < bitsToRead;
       i++, (*pulseIndex)++) {
    size_t bits =
        (size_t)((pulses[*pulseIndex] + (pulseDuration / 2)) / pulseDuration);

    for (size_t j = 0; j < bits; j++) {
      frame[bitsRead / 8] <<= 1;
      frame[bitsRead / 8] |= i % 2 == 0;
      bitsRead++;
      if (bitsRead >= bitsToRead) {
        return j + 1;
      }
    }
  }

  // Check if there are enough bits read
  return bitsRead >= bitsToRead ? 0 : -1;
}

bool checkSyncWord(const unsigned char synword[], const unsigned char pattern[], size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (synword[i] != pattern[i]) {
      return false;
    }
  }
  return true;
}

// TX
byte hexchar2hexvalue(char c)
{
   if ((c >= '0') && (c <= '9'))
      return c - '0';
   if ((c >= 'A') && (c <= 'F'))
      return c - 'A' + 10;
   if ((c >= 'a') && (c <= 'f'))
      return c - 'a' + 10;
   return -1 ;
}


// TX
bool* convertToBinary(const char* hex, size_t* resultSize)
{
    size_t len = strlen(hex);
    *resultSize = len * 4; // Each hex char is represented by 4 bits

    bool* binaryResult = (bool*)malloc(*resultSize * sizeof(bool));

    if (binaryResult == NULL)
    {
        Serial.printf(PLUGIN_077_ID ": Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    size_t index = 0;

    for (size_t i = 0; i < len; i++)
    {
        char hexChar = hex[i];
        int hexValue = hexchar2hexvalue(hexChar);

        for (int j = 3; j >= 0; j--)
        {
            bool bit = (hexValue >> j) & 1;
            binaryResult[index++] = bit;
        }
    }

    return binaryResult;
}

boolean Plugin_077(byte function, const char *string)
{
   if (RawSignal.Number == 0)
   {
      return false;
   }

   const u_int16_t AVTK_PulseDuration = AVTK_PULSE_DURATION_MID_D / RawSignal.Multiply;
   const u_int16_t AVTK_PulseMinDuration = AVTK_PULSE_DURATION_MIN_D / RawSignal.Multiply;
   const u_int16_t AVTK_PulseMaxDuration = AVTK_PULSE_DURATION_MAX_D / RawSignal.Multiply;
   const u_short AVTK_SyncPairsCount = 8;
   const u_short AVTK_MinSyncPairs = 5;
   // const u_short AVTK_MinSyncPairs = static_cast<u_short>(AVTK_SyncPairsCount / RawSignal.Multiply);

  const int syncWordSize = 8;
  unsigned char syncwordChars[] = {0xCA, 0xCA, 0x53, 0x53};
  size_t syncwordLength = sizeof(syncwordChars) / sizeof(syncwordChars[0]);
  uint8_t synword[syncwordLength];

  int pulseIndex = 1;
  bool oneMessageProcessed = false;

  while (pulseIndex + (int)(2 * AVTK_SyncPairsCount + syncWordSize) <
         RawSignal.Number) {
    u_short preamblePairsFound = 0;
    for (size_t i = 0; i < AVTK_SyncPairsCount; i++) {
      if (value_between(RawSignal.Pulses[pulseIndex], AVTK_PulseMinDuration,
                        AVTK_PulseMaxDuration) &&
          value_between(RawSignal.Pulses[pulseIndex + 1], AVTK_PulseMinDuration,
                        AVTK_PulseMaxDuration)) {
        preamblePairsFound++;
      } else if (preamblePairsFound > 0) {
        // if we didn't already had a match, we ignore as mismatch, otherwise we
        // break here
        break;
      }
      pulseIndex += 2;
    }

    if (preamblePairsFound < AVTK_MinSyncPairs) {
#ifdef PLUGIN_077_DEBUG
      Serial.printf(PLUGIN_077_ID ": Preamble not found (%i < %i)\n", preamblePairsFound,
             AVTK_MinSyncPairs);
#endif
      return oneMessageProcessed;
    }
#ifdef PLUGIN_077_DEBUG
    Serial.printf(PLUGIN_077_ID ": Preamble found (%i >= %i)\n", preamblePairsFound,
           AVTK_MinSyncPairs);
#endif

    uint8_t bitsProccessed =
        decode_bits(synword, RawSignal.Pulses, RawSignal.Number, &pulseIndex,
                    AVTK_PULSE_DURATION_MID_D, 8 * syncwordLength);
    if (!bitsProccessed) {
#ifdef PLUGIN_077_DEBUG
      Serial.printf(PLUGIN_077_ID ": Error on syncword decode\n");
#endif
      return oneMessageProcessed;
    }

#ifdef PLUGIN_077_DEBUG
    Serial.printf(PLUGIN_077_ID ": Syncword 0x");
    for (size_t i = 0; i < syncwordLength; i++) {
      Serial.printf("%02X", syncwordChars[i]);
    }
#endif

    if (!checkSyncWord(synword, syncwordChars, syncwordLength)) {
#ifdef PLUGIN_077_DEBUG
      Serial.printf(" not found\n");
#endif
      return oneMessageProcessed;
    }
#ifdef PLUGIN_077_DEBUG
    Serial.printf(" found\n");
#endif

    int alteredIndex = pulseIndex;
    uint16_t alteredValue = RawSignal.Pulses[pulseIndex];
    if (isLowPulseIndex(pulseIndex)) {
      // the last pulse "decode_bits" processed was high
      RawSignal.Pulses[pulseIndex] =
          RawSignal.Pulses[pulseIndex] - bitsProccessed * AVTK_PulseDuration;
    }

    byte address[] = { 0, 0, 0, 0 };

    bool decodeResult = decode_manchester(
        address, 32, RawSignal.Pulses, RawSignal.Number, &pulseIndex, AVTK_PulseMinDuration,
        AVTK_PulseMaxDuration, 2 * AVTK_PulseMinDuration,
        2 * AVTK_PulseMaxDuration, 0, true);
    RawSignal.Pulses[alteredIndex] = alteredValue;

    if (!decodeResult) {
#ifdef PLUGIN_077_DEBUG
      Serial.printf(PLUGIN_077_ID ": Could not decode address manchester data\n");
#endif
      return oneMessageProcessed;
    }
#ifdef PLUGIN_077_DEBUG
    Serial.printf(PLUGIN_077_ID ": Address (lsb): %02x %02x %02x %02x\n", address[0], address[1],
           address[2], address[3]);
    Serial.printf(PLUGIN_077_ID ": pulseIndex is %i\n", pulseIndex);
#endif

    byte buttons[] = { 0 };
    if (!decode_manchester(buttons, 1, RawSignal.Pulses, RawSignal.Number, &pulseIndex,
                           AVTK_PulseMinDuration, AVTK_PulseMaxDuration,
                           2 * AVTK_PulseMinDuration, 2 * AVTK_PulseMaxDuration,
                           0, true)) {
#ifdef PLUGIN_077_DEBUG
#endif
      Serial.printf(PLUGIN_077_ID ": Could not decode buttons manchester data\n");
      return oneMessageProcessed;
    }
// TODO we would have to shift back the result because we shifted it too much to
// the left because we think that everything has 8 bits
#ifdef PLUGIN_077_DEBUG
    Serial.printf(PLUGIN_077_ID ": Buttons: %02x\n", buttons[0]);
    Serial.printf(PLUGIN_077_ID ": pulseIndex is %i\n", pulseIndex);
#endif

    // pulseIndex += 7; // CRC
    // pulseIndex += 3; // ???
    int remainingPulsesCount = 7 + 3;

    size_t remaining[remainingPulsesCount];
    for (int i = 0; i < remainingPulsesCount; i++) {
      remaining[i] =
          (RawSignal.Pulses[pulseIndex++] + AVTK_PulseDuration / 2) / AVTK_PulseDuration;
    }

#ifdef PLUGIN_077_DEBUG
    Serial.printf(PLUGIN_077_ID ": remaining ");
    for (int i = 0; i < remainingPulsesCount; i++) {
      Serial.printf("%i ", remaining[i]);
    }
    Serial.printf("\n");
    Serial.printf(PLUGIN_077_ID ": pulseIndex is %i\n", pulseIndex);
#endif

   display_Header();
   display_Name(PLUGIN_077_ID);
   char c_ID[4 * 2 + 1];
   sprintf(c_ID, "%02x%02x%02x%02x", address[0], address[1], address[2], address[3]);
   display_IDc(c_ID);
   display_SWITCH(buttons[0]);
   display_Footer();

    oneMessageProcessed = true;
  }

  return oneMessageProcessed;
}
#endif //PLUGIN_077

#ifdef PLUGIN_TX_077
#include "../1_Radio.h"
#include "../2_Signal.h"
#include "../3_Serial.h"
#include <stdlib.h>

inline void send(boolean state)
{
   digitalWrite(Radio::pins::TX_DATA, state ? HIGH : LOW);
   delayMicroseconds(AVTK_PULSE_DURATION_MID_D);
}

size_t preambleSize;
size_t syncWordSize;
bool* preamble = convertToBinary("aaaa", &preambleSize);
bool* syncWord = convertToBinary("caca5353", &syncWordSize);

boolean PluginTX_077(byte function, const char *string)
{
   //10;AVANTEK;71f1100080
   //012345678901234567890
   if (strncasecmp(InputBuffer_Serial + 3, "AVANTEK;", 8) == 0) {
		short times = 3;
		char* address = InputBuffer_Serial + 3 + 8;

		noInterrupts();

#ifdef PLUGIN_077_DEBUG
    Serial.println(F(PLUGIN_077_ID ": Sending preamble"));
#endif
		for (u_short count = 0; count < times; count++) {
         for (u_int i = 0; i < preambleSize; i++) {
            send(preamble[i]);
         }

#ifdef PLUGIN_077_DEBUG
         Serial.println(F(PLUGIN_077_ID ": Sending syncword"));
#endif
         for (u_int i = 0; i < syncWordSize; i++) {
            send(syncWord[i]);
         }

#ifdef PLUGIN_077_DEBUG
         Serial.print(F(PLUGIN_077_ID ": Sending payload "));
         Serial.print(address);
#endif
			for (size_t i = 0; i < strlen(address); i++) {
				char hexChar = address[i];
				int hexValue = hexchar2hexvalue(hexChar);

				for (int j = 3; j >= 0; j--) {
					bool bit = (hexValue >> j) & 1;
					if (bit) {
						send(true);
						send(false);
						send(false);
					} else {
						send(true);
						send(true);
						send(false);
					}
				}
			}
         // TODO introduce as constant
         delayMicroseconds(5 * AVTK_PULSE_DURATION_MID_D);
		}
		interrupts();

    return true;
	}
   return false;
}

#endif //PLUGIN_TX_077
