#pragma once
#include <ctype.h>
#include <string.h>
namespace ble_json_validation {
bool number(JsonVariantConst field, uint32_t maximum, uint32_t minimum = 0) {
  return field.is<uint32_t>() && !field.is<bool>() && field.as<uint32_t>() >= minimum && field.as<uint32_t>() <= maximum;
}
bool color(JsonVariantConst field) {
  if (!field.is<const char*>()) return false;
  const char* text = field.as<const char*>();
  size_t length = strlen(text);
  if (length != 6 && length != 8) return false;
  for (size_t index = 0; index < length; ++index) if (!isxdigit(static_cast<unsigned char>(text[index]))) return false;
  return true;
}
bool individual(JsonArrayConst colors, uint32_t count) {
  if (!colors.size()) return false;
  size_t index = 0;
  while (index < colors.size()) {
    if (!number(colors[index], count - 1)) return false;
    uint32_t start = colors[index++].as<uint32_t>();
    if (index < colors.size() && colors[index].is<uint32_t>()) {
      if (!number(colors[index], count, start + 1)) return false;
      ++index;
    }
    if (index >= colors.size() || !color(colors[index++])) return false;
  }
  return true;
}
bool segment(JsonObjectConst object, uint32_t count) {
  if (object.isNull() || !number(object["id"], 255)) return false;
  for (JsonPairConst entry : object) {
    const char* key = entry.key().c_str();
    JsonVariantConst field = entry.value();
    if (!strcmp(key,"id") || !strcmp(key,"bri") || !strcmp(key,"fx") || !strcmp(key,"pal")) { if (!number(field,255)) return false; }
    else if (!strcmp(key,"start")) { if (!number(field,count-1)) return false; }
    else if (!strcmp(key,"stop")) { if (!number(field,count,1)) return false; }
    else if (!strcmp(key,"grp")) { if (!number(field,65535,1)) return false; }
    else if (!strcmp(key,"spc") || !strcmp(key,"of")) { if (!number(field,65535)) return false; }
    else if (!strcmp(key,"on") || !strcmp(key,"rev") || !strcmp(key,"mi") || !strcmp(key,"frz")) { if (!field.is<bool>()) return false; }
    else if (!strcmp(key,"i")) { if (!field.is<JsonArrayConst>() || !individual(field.as<JsonArrayConst>(),count)) return false; }
    else if (!strcmp(key,"col")) {
      if (!field.is<JsonArrayConst>()) return false;
      JsonArrayConst colors = field.as<JsonArrayConst>();
      if (!colors.size() || colors.size() > 3) return false;
      for (JsonVariantConst channels : colors) {
        if (!channels.is<JsonArrayConst>() || channels.size() < 3 || channels.size() > 4) return false;
        for (JsonVariantConst channel : channels.as<JsonArrayConst>()) if (!number(channel,255)) return false;
      }
    } else return false;
  }
  if (object.containsKey("start") && object.containsKey("stop") && object["start"].as<uint32_t>() >= object["stop"].as<uint32_t>()) return false;
  return true;
}
bool lighting(JsonObjectConst object, uint32_t count) {
  if (object.isNull() || !object.size() || !count) return false;
  for (JsonPairConst entry : object) {
    const char* key = entry.key().c_str();
    JsonVariantConst field = entry.value();
    if (!strcmp(key,"on")) { if (!field.is<bool>()) return false; }
    else if (!strcmp(key,"bri")) { if (!number(field,255)) return false; }
    else if (!strcmp(key,"tt") || !strcmp(key,"transition")) { if (!number(field,65535)) return false; }
    else if (!strcmp(key,"live")) { if (!field.is<bool>() || field.as<bool>()) return false; }
    else if (!strcmp(key,"lor")) { if (!number(field,1,1)) return false; }
    else if (!strcmp(key,"nl")) {
      if (!field.is<JsonObjectConst>() || field.size()!=1 || !field["on"].is<bool>()) return false;
    } else if (!strcmp(key,"udpn")) {
      if (!field.is<JsonObjectConst>() || field.size()!=1 || !field["nn"].is<bool>() || !field["nn"].as<bool>()) return false;
    } else if (!strcmp(key,"seg")) {
      if (!field.is<JsonArrayConst>() || !field.size() || field.size()>256) return false;
      for (JsonVariantConst item : field.as<JsonArrayConst>()) if (!item.is<JsonObjectConst>() || !segment(item.as<JsonObjectConst>(),count)) return false;
    } else return false;
  }
  return true;
}
}
