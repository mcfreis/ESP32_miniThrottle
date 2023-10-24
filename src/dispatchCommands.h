#pragma once

void setInitialData();
void invalidateRoutes (int8_t turnoutNr, char state);
void routeInitiate (int8_t routeNr);
bool routeSetup (int8_t routeNr, bool displayable);
void routeConfirm (int8_t routeNr);
void routeDeactivate (uint8_t routeNr);

void setTrackPower (uint8_t desiredState);
bool setTurnout (uint8_t turnoutNr, const char desiredState);
void setRoute (uint8_t routeNr);
bool invalidLocoIndex (const uint8_t locoIndex, const char *description);
void setLocoFunction (uint8_t locoIndex, uint8_t funcIndex, bool set);
void setLocoOwnership(uint8_t locoIndex, bool owned);
void setStealLoco(uint8_t locoIndex);
void setLocoSpeed (uint8_t locoIndex, int16_t speed, int8_t direction);
void setLocoDirection (uint8_t locoIndex, uint8_t direction);
void setDisconnected();
void getAddress();
void getCV(int16_t cv);
void setRouteBg (void *pvParameters);
