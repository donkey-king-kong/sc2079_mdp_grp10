#!/bin/bash

# Bluetooth setup for Raspberry Pi
sudo sdptool add --channel=2 SP
# sudo systemctl start rfcomm
sudo rfcomm release 0
sudo pkill rfcomm
sudo rfcomm listen /dev/rfcomm0 2