# ESP-01-Based-Smart-Water-Sprayer
ESP-01 based controller for electronic sprayer bottle

The brainchild of my 3 year old requesting a remote controlled water sprayer! 

Using parts I had to hand I came up with this very basic (but functional) circuit to directly mimic a physical press of the momentary button used to toggle power to the sprayer pump using a cheap Chinese tlp176a opto-isolating relay to briefly short across the sprayers momentary switch contacts (the sprayer PCB has a latching circuit and so a second momentary button press is needed to turn the pump off again).

The ESP-01 is powered by a HT7333 LDO voltage regulator driven by the sprayers own 18650 Li-ion cell with a 10uf tantalum smoothing capacitor. The supplied 18650 is of pretty low capacity but 24hrs standby time appears to be possible... A decent 3500mAh cell lasts closer 48hrs. obviously these timings will drop with greater pump usage.

The Photos (but not the schematic) show a set of 4 header pins (used for initial code upload via a FTDL adapter) and a switch which simply connects GPIO0 to GND for code upload... Both of which are redundant following initial code upload as I've added OTA upload and could have been removed altogether.

Coding wise I am a beginner really and as such heavily reliant on ChatGPT for most of this... there could well still be bugs left to find!

A Wifi AP point is created, accessible at 192.168.4.1 where WIFI STAs can be configured along with MQTT server and Topic details can be set if required. Webserver control is accessible via IP assigned by router (or http://smart_sprayer/) or 192.168.4.1 (if connected to AP)
MQTT publishes online/offline status to 'sprayer/status' whilst control is possible by publishing messages to the cmd topic 'sprayer/button/cmd' - or as set in AP / wifi config page with value format ON:duration (in ms e.g.) Topic: 'sprayer/button/cmd' Value: 'ON:1000'

Unless changed in code... The default spray time= 1000ms (1sec) Min spray time= 250ms (1/4sec) and Max spray time = 120000ms (2min).

<img width="1190" height="845" alt="Based Smart Spray Controller_6" src="https://github.com/user-attachments/assets/c9966589-2248-4f03-b252-339981e2a3b6" />           
<img width="1190" height="845" alt="Based Smart Spray Controller_Schematic" src="https://github.com/user-attachments/assets/c544ffe2-dd0d-4137-b59c-a2e04908ffbe" />
<img width="1190" height="845" alt="Based Smart Spray Controller_4" src="https://github.com/user-attachments/assets/90ae5bb0-6ee4-4a51-a113-25fddf48911c" />
<img width="1190" height="845" alt="Based Smart Spray Controller_2" src="https://github.com/user-attachments/assets/b61831fa-6538-4722-bc4b-a578dcd98deb" />
<img width="1190" height="845" alt="Based Smart Spray Controller_3" src="https://github.com/user-attachments/assets/fdd5bba5-dc07-4a04-843a-0585a9b0abd6" />
<img width="1190" height="845" alt="Based Smart Spray Controller_5" src="https://github.com/user-attachments/assets/19ed2201-7a13-4114-9a57-92671b13be93" />
<img width="1190" height="845" alt="Based Smart Spray Controller_1" src="https://github.com/user-attachments/assets/e5324d1b-6a76-4dea-83e9-14410d417785" />

As this was a very quick throw it together project to keep a 3 y.o. interested & happy it is very rough and ready! it has, though, proved to work even better than I'd expected.
