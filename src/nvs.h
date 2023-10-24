#pragma once

void nvs_init();
void nvs_get_string (const char *strName, char *strDest, const char *strDefault, int strSize);
void nvs_get_string (const char *strName, char *strDest, int strSize);
void nvs_put_string (const char *strName, const char *value);
int nvs_get_int (const char *intName, int intDefault);
void nvs_put_int (const char *intName, int value);
double nvs_get_double (const char *doubleName, double doubleDefault);
void nvs_put_double (const char *doubleName, double value);
float nvs_get_float (const char *floatName, float floatDefault);
void nvs_put_float (const char *floatName, float value);
int nvs_get_freeEntries();
void nvs_put_string (const char* nameSpace, const char* key, const char* value);
void nvs_get_string (const char* nameSpace, const char *strName, char *strDest, const char *strDefault, int strSize);
void nvs_del_key (const char* nameSpace, const char* key);
void* nvs_extractStr (const char *nameSpace, int numEntries, int dataSize);
int nvs_count (const char* target, const char* type);
void nvs_dumper(const char *target);
