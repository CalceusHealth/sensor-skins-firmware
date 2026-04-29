Hey mate,

Here's the source code.

The battery query is ;QL\r\n (for the \r\n if using nRF Tools on your phone, just hit enter on the phone keyboard before you hit send). This should return the battery voltage in mV.

#####

Instructions:

Install Segger Embedded Studio (SES); I use SES for Arm 4.18, but have also used SES 8.10d with success

Download and unzip nRF5 SDK 15.3 - I just unzip it to a convenient location like my Downloads folder.

Unzip the firmware into the location [nRF5 SDK 15.3 folder]\examples\ble_peripheral\reid_ble_aginic_v2\pca10040\s112

Modify the batch file to reference your new top-level 15.3 folder location

Open examples\ble_peripheral\reid_ble_aginic_v2\pca10040\s112\ses\ble_app_aginic_v2.emProject and SES should open. Make your changes and hit F7 to compile.

Run the batch file to generate the DFU zip file. At the moment LHS and RHS are separate, so you’ll need to repeat this with the other one selected in configure_firmware.h - we have hardware support to detect LHS vs RHS on the PCB itself but I wanted to hard code it for now while we are in the testing stage (don’t want the pin to be a dry joint and cause a complete failure of the orthotic).

Load the DFU zip file on your phone.

Open nRF DFU app on your phone, and select the DFU zip file.

Connect to the orthotic on your phone using nRF Tools and issue ;CR\r\n - the orthotic should disconnect

(quickly, since you are now on a timer) Swap back to the nRF DFU app, select device, and search for SS_DFU - select it, and then hit Start.

#####

Here's the definition for the battery states:

typedef enum battery_state_t
{
     battery_ok = 'K',
     battery_charging = 'C',
     battery_charged = 'D',
     battery_low = 'L',
     battery_unknown = 'U'
} battery_state_t;

I've also attached the new firmwares for you.

#####

Hey Alex,
Got to apologise for any terminology I screw up here, still learning the lingo. The attached report is also pretty much 100% model generated, so call out any BS.

I've been testing over the past week or so and am now up to the battery testing. I had noticed on the first batch of 2 x Mediums, that the battery level never really changed much when I queried it. The devices would last for a few days after charging overnight, but I was never able to get any meaningful data from the battery queries and into the testing or mobile apps. The values were always around 3.28-3.31V. Because I was focussed on the sensor data and I was just charging the devices when they went flat, I didn't really apply any logical testing to the battery until today. 

What I've done is got the Routejade FLPB301031 datasheet, then checked that against the firmware using a closed model. No surprise your firmware code is correct, but there was a consistent difference in the measured battery value from the test pins using a multimeter vs the battery value from the BLE query.
Lookup tables (discharge + charge curves) — correct, derived from Routejade datasheet
ADC formula logic — correct, right gain setting, right reference voltage, right approach
VDIV_VBAT_R1/R2 constants — incorrect, set to 1:1 (assumes 50% divider) but the actual assembled PCB divider measures ~43.5%
I can move on with more testing, and in the mobile/testing apps apply this correction factor so the voltage and % is more aligned with the measured multimeter values, but wondered if this makes sense and is something we can update in the firmware before batch 3? If this is easier to discuss just give me a call or we can chat Wednesday.

Hi Tim,

It looks like it might actually be a firmware bug - not turning on VBAT_ON before taking the measurement. We can try and fix this in the next version first.

Let me know if you would like an updated firmware release to test ahead of the next batch.