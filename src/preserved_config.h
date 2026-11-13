#pragma once
namespace ble_json {
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
