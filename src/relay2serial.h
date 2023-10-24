#pragma once

void relayListener(void *pvParameters);
void reply2relayNode (struct relayConnection_s *thisRelay, const char *outPacket);
void relay2WiThrot (char *outBuffer);