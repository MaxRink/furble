#ifndef FURBLE_HOST_MQTT_CJSON_H
#define FURBLE_HOST_MQTT_CJSON_H

#include <cstddef>

struct cJSON {
  cJSON *next = nullptr;
  cJSON *prev = nullptr;
  cJSON *child = nullptr;
  int type = 0;
  char *valuestring = nullptr;
  int valueint = 0;
  double valuedouble = 0;
  char *string = nullptr;
};

enum {
  cJSON_Invalid = 0,
  cJSON_False = 1 << 0,
  cJSON_True = 1 << 1,
  cJSON_NULL = 1 << 2,
  cJSON_Number = 1 << 3,
  cJSON_String = 1 << 4,
  cJSON_Array = 1 << 5,
  cJSON_Object = 1 << 6,
};

extern "C" {
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *value);
cJSON *cJSON_CreateNumber(double value);
cJSON *cJSON_AddObjectToObject(cJSON *object, const char *name);
cJSON *cJSON_AddArrayToObject(cJSON *object, const char *name);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int value);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double value);
void cJSON_AddItemToObject(cJSON *object, const char *name, cJSON *item);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *name);
int cJSON_IsNumber(const cJSON *item);
cJSON *cJSON_ParseWithLength(const char *value, size_t length);
char *cJSON_PrintUnformatted(const cJSON *item);
void cJSON_Delete(cJSON *item);
void cJSON_free(void *item);
}

#endif
