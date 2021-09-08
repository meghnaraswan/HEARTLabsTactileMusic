
# This file is executed on every boot (including wake-boot from deepsleep)

import esp
import ubluetooth
import struct
import time
import machine
from machine import Pin
from micropython import const

#esp.osdebug(None)

ble = ubluetooth.BLE()
ble.active(True)
ble.config(gap_name='Haptic Sleeve')
