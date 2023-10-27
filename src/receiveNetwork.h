#pragma once

void receiveNetData(void *pvParameters);
void processPacket (char *packet);

bool netConnState (uint8_t chkmode);