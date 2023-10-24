#pragma once

bool showPinConfig();
void serialConsole(void *pvParameters);
void txPacket (const char *header, const char *pktData);
void txPacket (const char *pktData);
void mt_dump_data (int nparam, const char **param);
void processSerialCmd (uint8_t *inBuffer);