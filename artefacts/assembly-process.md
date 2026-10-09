# Assembly process: charger puck and orthotic sensor layer

Current production assembly steps, as supplied 2026-10-01. Supersedes the
early-build notes in `carbon-circuits-1`, which record the first coil
attachment as "No strain relief added".

## Charging puck

- SMT (0402 components, 0.4 mm pitch QFN, fairly simple)
- Hand processing plus minor soldering:
  - Put a drop of superglue in the centre of the coil and fit a 4 × 3 mm magnet
  - Apply 3M tape to the back of the coil
  - Solder the coil to the PCB
  - Tape it down in the centre
- Program the unit
- Functional/performance test: power up, check current, check the LED
  response when placed on the coil
- Pot the unit into its housing (hot-melt adhesive for now; interim
  prototype solution)

## Orthotic sensor layer

- SMT (0402 components, 0.4 mm pitch QFN)
- Through-hole soldering: fit the sensor layer (2 × 20-pin crimped terminals)
- Hand processing plus soldering, battery:
  - Fit tape to the battery
  - Solder the battery to the board
  - Tape the battery to the board
- Hand processing plus soldering, coil:
  - Put a drop of superglue in the centre of the coil and fit a 4 × 3 mm magnet
  - Fit 10 mm heatshrink to the coil leads
  - Bend the coil leads backwards into a C shape
  - Solder the coil leads to the PCB
  - Glue both ends of the heatshrink
- Program the unit
- Functional/performance test: more involved; needs a BLE connection and a
  check of sensor response
