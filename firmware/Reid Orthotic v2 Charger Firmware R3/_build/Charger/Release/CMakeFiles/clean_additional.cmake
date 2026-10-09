# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "/home/grey/Documents/2026/work/calceusHealth/sensor-skins-firmware/firmware/Reid Orthotic v2 Charger Firmware R3/out/Charger/Release.eep"
  "/home/grey/Documents/2026/work/calceusHealth/sensor-skins-firmware/firmware/Reid Orthotic v2 Charger Firmware R3/out/Charger/Release.hex"
  "/home/grey/Documents/2026/work/calceusHealth/sensor-skins-firmware/firmware/Reid Orthotic v2 Charger Firmware R3/out/Charger/Release.lss"
  "/home/grey/Documents/2026/work/calceusHealth/sensor-skins-firmware/firmware/Reid Orthotic v2 Charger Firmware R3/out/Charger/Release.srec"
  "/home/grey/Documents/2026/work/calceusHealth/sensor-skins-firmware/firmware/Reid Orthotic v2 Charger Firmware R3/out/Charger/Release.usersignatures"
  )
endif()
