/*
MIT License

Copyright (c) [2021] [Enfield Cat]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 * Keep NVS storage routines here
 */
Preferences prefs;

void nvs_init()
{
  prefs.begin ("Throttle");  
}

void nvs_get_string (char *strName, char *strDest, char *strDefault, int strSize)
{
  if (prefs.getString(strName, strDest, strSize) == 0) {
    strcpy (strDest, strDefault);
  }  
}

void nvs_get_string (char *strName, char *strDest, int strSize)
{
  prefs.getString(strName, strDest, strSize);
}

void nvs_put_string (char *strName, char *value)
{
  char oldval[80];
  oldval[0] = '\0';
  nvs_get_string (strName, oldval, "", 80);
  if (strcmp (oldval, value) != 0) {
    prefs.putString (strName, value);
    configHasChanged = true;
  }
}

int nvs_get_int (char *intName, int intDefault)
{
  return (prefs.getInt (intName, intDefault));
}

void nvs_put_int (char *intName, int value)
{
  int oldval = nvs_get_int (intName, value+1);
  if (value != oldval) {
    prefs.putInt (intName, value);
    configHasChanged = true;
  }
}

double nvs_get_double (char *doubleName, double doubleDefault)
{
  double retval = prefs.getDouble (doubleName, doubleDefault);
  if (isnan(retval)) retval = doubleDefault; 
  return (retval);
}

void nvs_put_double (char *doubleName, double value)
{
  double oldval = nvs_get_double (doubleName, value+1.00);
  if (value != oldval) {
    prefs.putDouble (doubleName, value);
    configHasChanged = true;
  }
}

float nvs_get_float (char *floatName, float floatDefault)
{
  float retval = prefs.getFloat (floatName, floatDefault);
  if (isnan(retval)) retval = floatDefault; 
  return (retval);
}

void nvs_put_float (char *floatName, float value)
{
  float oldval = nvs_get_float (floatName, value+1.00);
  if (value != oldval) {
    prefs.putFloat (floatName, value);
    configHasChanged = true;
  }
}

int nvs_get_freeEntries()
{
  return (prefs.freeEntries());
}


/*
 * Inspired by https://github.com/Edzelf/ESP32-Show_nvs_keys/blob/master/Show_nvs_keys.ino
 * Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
 */
void nvs_dumper(char *target)
{
  uint32_t nsoffset = 0;
  uint32_t daoffset = 0;
  esp_partition_iterator_t partIt;
  const esp_partition_t* part;
  char outline[105];
  char dataLine[41];
  char *dataPtr;
  char *typePtr;
  uint8_t i, j;
  uint8_t bitmap;
  uint8_t targetns;
  uint8_t page;
  nvs_page *nsbuff = NULL;
  nvs_page *dabuff = NULL;

  nsbuff = (nvs_page*) malloc (sizeof (nvs_page));
  dabuff = (nvs_page*) malloc (sizeof (nvs_page));
  partIt = esp_partition_find (ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, (const char*) "nvs");
  if (partIt) {
    part = esp_partition_get (partIt);
    esp_partition_iterator_release (partIt);
    sprintf (outline, "NVS partition size: %d bytes", part->size);
    if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
      Serial.println (outline);
      xSemaphoreGive(displaySem);
    }
    while (nsoffset < part->size) {
      if (esp_partition_read (part, nsoffset, nsbuff, sizeof(nvs_page)) != ESP_OK) {
        if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
          Serial.println ("Error reading NVS data");
          xSemaphoreGive(displaySem);
        }
        if (nsbuff != NULL) free (nsbuff);
        if (dabuff != NULL) free (dabuff);
        return;
      }
      i = 0 ;
      while (i < 126) {
        bitmap = ( nsbuff->Bitmap[i/4] >> ( ( i % 4 ) * 2 ) ) & 0x03 ;  // Get bitmap for this NameSpace entry
        if ( bitmap == 2 ) {
          if (nsbuff->Entry[i].Ns == 0 && (target==NULL || strcmp(target, nsbuff->Entry[i].Key)==0)) {
            sprintf (outline, "--- Namespace = %s ---------------------------------", nsbuff->Entry[i].Key);
            if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
              Serial.println (outline);
              sprintf (outline, "%4s | %3s | %-8s | %-15s | %s", "Page", "Key", "Type", "Name", "Value");
              Serial.println (outline);
              xSemaphoreGive(displaySem);
            }
            targetns = (uint8_t) (nsbuff->Entry[i].Data.sixFour & 0xFF);
            daoffset = 0;
            page = 0;
            while (daoffset < part->size) {
              if (esp_partition_read (part, daoffset, dabuff, sizeof(nvs_page)) != ESP_OK) {
                if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
                  Serial.println ("Error reading NVS data");
                  xSemaphoreGive(displaySem);
                }
                if (nsbuff != NULL) free (nsbuff);
                if (dabuff != NULL) free (dabuff);
                return;
              }
              j = 0;
              while (j < 126) {
                bitmap = ( dabuff->Bitmap[j/4] >> ( ( j % 4 ) * 2 ) ) & 0x03 ;  // Get bitmap for this Data entry
                if ( bitmap == 2 ) {
                  if (dabuff->Entry[j].Ns == targetns) {
                    switch (dabuff->Entry[j].Type) {
                      case 0x01:
                        sprintf (dataLine, "%d", dabuff->Entry[j].Data.eight[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x02:
                        sprintf (dataLine, "%d", dabuff->Entry[j].Data.oneSix[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x04:
                        sprintf (dataLine, "%d", dabuff->Entry[j].Data.threeTwo[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x08:
                        sprintf (dataLine, "%ld", dabuff->Entry[j].Data.sixFour);
                        dataPtr = dataLine;
                        break;
                      case 0x11:
                        sprintf (dataLine, "%d", (int8_t) dabuff->Entry[j].Data.eight[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x12:
                        sprintf (dataLine, "%d", (int16_t) dabuff->Entry[j].Data.oneSix[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x14:
                        sprintf (dataLine, "%d", (int32_t) dabuff->Entry[j].Data.threeTwo[0]);
                        dataPtr = dataLine;
                        break;
                      case 0x18:
                        sprintf (dataLine, "%ld", (int64_t) dabuff->Entry[j].Data.sixFour);
                        dataPtr = dataLine;
                        break;
                      case 0x21:
                        dataPtr = (char*) &(dabuff->Entry[j+1].Ns);
                        dataPtr[dabuff->Entry[j].Data.oneSix[0]] = '\0';
                        if (strlen(dataPtr) >= 60) dataPtr[59] = 0;
                        break;
                      default:
                        sprintf (dataLine, "0x%02x%02x%02x%02x%02x%02x%02x%02x",
                          dabuff->Entry[j].Data.eight[0],
                          dabuff->Entry[j].Data.eight[1],
                          dabuff->Entry[j].Data.eight[2],
                          dabuff->Entry[j].Data.eight[3],
                          dabuff->Entry[j].Data.eight[4],
                          dabuff->Entry[j].Data.eight[5],
                          dabuff->Entry[j].Data.eight[6],
                          dabuff->Entry[j].Data.eight[7]);
                        dataPtr = dataLine;
                        break;
                    }
                    typePtr = NULL;
                    for (uint8_t n=0; n<sizeof(nvs_index_ref) && typePtr==NULL; n++) if (nvs_index_ref[n] == dabuff->Entry[j].Type) typePtr = nvs_descrip[n];
                    if (typePtr == NULL) typePtr = (char*) "Unknown";
                    sprintf (outline, "%4d | %03d | %-8s | %-15s | %s", page,  j,// Print page and entry nr
                       typePtr,                                                  // Print the data type
                       dabuff->Entry[j].Key,                                     // Print the key
                       dataPtr );                                                // Print the data
                    if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
                      Serial.println (outline);
                      xSemaphoreGive(displaySem);
                    }
                  }
                j += dabuff->Entry[j].Span ;                                    // Next Data entry
                }
                else j++;
              }
              daoffset += sizeof(nvs_page) ;                                   // Prepare to read next Data page in nvs
              page++;
            }
          }
          i += nsbuff->Entry[i].Span ;                                          // Next NameSpace entry          
        }
        else i++ ;
      }
      nsoffset += sizeof(nvs_page) ;                                           // Prepare to read next NameSpace page in nvs
    }
  }
  else {
    if (xSemaphoreTake(displaySem, pdMS_TO_TICKS(2000)) == pdTRUE) {
      Serial.println ("NVS partition not found");
      xSemaphoreGive(displaySem);
    }
  }
  if (nsbuff != NULL) free (nsbuff);
  if (dabuff != NULL) free (dabuff);
}
