# Fan
Arduino program for Sonoff TH10 controlling a fan and with a humidity sensor. Control via MQTT

Secrets are stored in secrets.h

Rest of setup is in header of the .ino file

Controlled via MQTT
mqttTopicAnnounce     "fan/announce"     When the fan controller boots it announces itself here
mqttTopicState        "fan/state"        Fan controller reports its fan state on/off
mqttTopicRising       "fan/rising"       Fan controller reports current setting for rising humidity threshold
mqttTopicFalling      "fan/falling"      Fan controller reports current setting for falling humidity threshold
mqttTopicSet          "fan/set"          This topic controls the fan on or off. Fans stays on for 10 minutes (fart mode)
mqttTopicSetRising    "fan/set/rising"   Set the rising threshold for humidity
mqttTopicSetFalling   "fan/set/falling"  Set the falling threshold for humidity
mqttTopicSetReport    "fan/set/report"   Any message sent to this topic makes the controller report current thresholds
mqttTopicTemperature  "fan/temperature"  Fan controller reports the temperature every 2 minutes
mqttTopicHumidity     "fan/humidity"     Fan controller reports the humidity every 2 minutes
