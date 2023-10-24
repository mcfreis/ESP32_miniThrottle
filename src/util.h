#include <Arduino.h>

#pragma once

int util_str2int(char *string);
bool util_str_isa_int (char *inString);
float util_str2float (char *string);
char* util_ftos (float value, int dp);
char* util_bin2str (uint8_t inVal);
char* getTimeStamp();
void timeFormat (char *tString, uint32_t tint);
void semFailed (const char *semName, const char *fileName, const int line);
void swapRecord (void *lowRec, void *hiRec, int recSize);
void sortLoco();
void sortTurnout();
void sortTurnout(struct turnout_s *turnoutList, uint8_t turnoutCount);
void sortRoute();
