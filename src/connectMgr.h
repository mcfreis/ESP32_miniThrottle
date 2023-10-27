#pragma once

void wifi_scanNetworks();
void wifi_scanNetworks(bool echo);
void initDccEx();

void txPacket (const char *header, const char *pktData);
void txPacket (const char *pktData);

void connectionManager(void *pvParameters);
void serialConnectionManager(void *pvParameters);

void mdnsLookup (const char *service);
int mdnsLookup (const char *service, char *addr);

void connect2server (char *server, int port);