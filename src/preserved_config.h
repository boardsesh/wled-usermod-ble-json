#pragma once
namespace ble_json {
template<typename Flag>
bool readBleEnabled(JsonObjectConst root, Flag& enabled) {
  const auto setting=root["BLE JSON"]["enabled"];
  if (!setting.template is<bool>()) return false;
  enabled=setting.template as<bool>();
  return true;
}
// A partial usermod settings update omits unrelated objects; omission is not deletion.
template<typename Text>
void rememberDormantAudio(JsonObjectConst root, Text& stored) {
  if (!root["AudioReactive"].is<JsonObjectConst>()) return;
  Text replacement;
  serializeJson(root["AudioReactive"],replacement);
  stored=replacement;
}
template<typename Text>
void restoreDormantAudio(JsonObject root, const Text& stored) {
  if (stored.length()) root["AudioReactive"]=serialized(stored);
}
}
