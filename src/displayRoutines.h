#pragma once

extern const uint8_t fontWidth[]; 
extern const size_t num_fontWidth;
extern const uint8_t fontHeight[];

uint8_t displayTempMessage (const char *header, const char *message, bool wait4response);
uint8_t displayMenu (const char **menuItems, uint8_t itemCount, uint8_t selectedItem);
uint8_t displayMenu (const char **menuItems, uint8_t *menuIndex, uint8_t itemCount, uint8_t selectedItem);
bool displayYesNo (const char *question);
void displayScreenLine (const char *menuItem, uint8_t lineNr, bool inverted);

void mkLocoMenu();
void mkTurnoutMenu();
void mkRouteMenu();
void mkPowerMenu();
void mkConfigMenu();
void mkCVMenu();
void mkFontMenu();
void mkSpeedStepMenu();
void mkCpuSpeedMenu();
void mkProtoPref();
uint8_t mkCabMenu();
void mkRotateMenu();
void displayInfo();

void setupFonts();
int enterNumber(const char *prompt);
char* enterAddress(const char *prompt);
