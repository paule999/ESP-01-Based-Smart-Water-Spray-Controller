# ESP-01-Based-Smart-Water-Sprayer
ESP-01 based controller for electronic sprayer bottle

The brainchild of my 3 year old requesting a remote controlled water sprayer! 

Using parts I had to hand I came up with this very basic (but nonetheless very effective) circuit to directly mimic a physical press of the momentary button used to toggle power to the sprayer pump using a cheap Chinese tlp176a opto-isolating relay to briefly short across the sprayers momentary switch contacts (the sprayer PCB has a latching circuit and so a second momentary button press is needed to turn the pump off again).

The ESP-01 is powered by a HT7333 LDO voltage regulator driven by the sprayers own 18650 Li-ion cell with a 10uf tantalum smoothing capacitor. The supplied 18650 is of pretty low capacity but 24hrs standby time appears to be possible... A decent 3500mAh cell lasts closer 48hrs. obviously these timings will drop with greater pump usage.

Coding wise I am a beginner really and as such heavily reliant on ChatGPT for most of this... there could well still be bugs left to find!

A Wifi AP point is created, accessible at 192.168.4.1 where WIFI STAs can be configured along with MQTT server and Topic details can be set if required. Webserver control is accessible via IP assigned by router (or http://smart_sprayer/) or 192.168.4.1 (if connected to AP)
MQTT publishes online/offline status to 'sprayer/status' whilst control is possible by publishing messages to the cmd topic 'sprayer/button/cmd' - or as set in AP / wifi config page with value format ON:duration (in ms e.g.) Topic: 'sprayer/button/cmd' Value: 'ON:1000'

Unless changed in code... The default spray time= 1000ms (1sec) Min spray time= 250ms (1/4sec) and Max spray time = 120000ms (2min).
            
