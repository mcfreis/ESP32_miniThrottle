#pragma once

void processDccPacket (char *packet);
void dccPopulateLoco();
void dccPopulateTurnout();
void dccPopulateRoutes();
void dccPowerChange(char state);