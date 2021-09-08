#main.py is the file that gets loaded onto the ESP32.  
#Gets run after successful boot.
#For receiving vibrotactile equalizer commands.
#Change name to main.py before uploading, else gets erased from memory.

from machine import Pin
from machine import PWM
from time import sleep
from micropython import const
import ubluetooth
import struct
import esp #copying from Grant's code


#Initializing PWM
up = PWM(Pin(18))
left = PWM(Pin(19))
down = PWM(Pin(22))
right = PWM(Pin(23))

up_duty = 0
left_duty = 0
down_duty = 0
right_duty = 0

up.freq(1000)
left.freq(1000)
down.freq(1000)
right.freq(1000)

up.duty(up_duty)
left.duty(left_duty)
down.duty(down_duty)
right.duty(right_duty)

def processWrite(message):
  global up_duty
  global left_duty
  global down_duty
  global right_duty
  
  up = int.from_bytes(message[0:2], 'big')
  left = int.from_bytes(message[2:4], 'big')
  down = int.from_bytes(message[4:6], 'big')
  right = int.from_bytes(message[6:8], 'big')
  
  up_duty = up
  left_duty = left
  down_duty = down
  right_duty = right

#Bluetooth Event Codes
_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)
_IRQ_GATTS_READ_REQUEST = const(4)
_IRQ_SCAN_RESULT = const(5)
_IRQ_SCAN_DONE = const(6)
_IRQ_PERIPHERAL_CONNECT = const(7)
_IRQ_PERIPHERAL_DISCONNECT = const(8)
_IRQ_GATTC_SERVICE_RESULT = const(9)
_IRQ_GATTC_SERVICE_DONE = const(10)
_IRQ_GATTC_CHARACTERISTIC_RESULT = const(11)
_IRQ_GATTC_CHARACTERISTIC_DONE = const(12)
_IRQ_GATTC_DESCRIPTOR_RESULT = const(13)
_IRQ_GATTC_DESCRIPTOR_DONE = const(14)
_IRQ_GATTC_READ_RESULT = const(15)
_IRQ_GATTC_READ_DONE = const(16)
_IRQ_GATTC_WRITE_DONE = const(17)
_IRQ_GATTC_NOTIFY = const(18)
_IRQ_GATTC_INDICATE = const(19)
_IRQ_GATTS_INDICATE_DONE = const(20)
_IRQ_MTU_EXCHANGED = const(21)

#Advertising Codes
# Declare appearance as a generic computer
_ADV_APPEARANCE_GENERIC_COMPUTER = const(128)

# Declare advertising codes
_ADV_TYPE_FLAGS = const(0x01)
_ADV_TYPE_NAME = const(0x09)
_ADV_TYPE_UUID16_COMPLETE = const(0x3)
_ADV_TYPE_UUID32_COMPLETE = const(0x5)
_ADV_TYPE_UUID128_COMPLETE = const(0x7)
_ADV_TYPE_APPEARANCE = const(0x19)

# Generate a payload to be passed to gap_advertise(adv_data=...)
#https://github.com/micropython/micropython/blob/master/examples/bluetooth/ble_advertising.py
def advertising_payload(limited_disc=False, br_edr=False, name=None, services=None, appearance=0):
  payload = bytearray()

  def _append(adv_type, value):
    nonlocal payload
    payload += struct.pack("BB", len(value) + 1, adv_type) + value

  _append(
    _ADV_TYPE_FLAGS,
    struct.pack("B", (0x01 if limited_disc else 0x02) + (0x00 if br_edr else 0x04)),
  )


  if name:
    _append(_ADV_TYPE_NAME, name)

  if services:
    for uuid in services:
      b = bytes(uuid)
      if len(b) == 2:
        _append(_ADV_TYPE_UUID16_COMPLETE, b)
      elif len(b) == 4:
        _append(_ADV_TYPE_UUID32_COMPLETE, b)
      elif len(b) == 16:
        _append(_ADV_TYPE_UUID128_COMPLETE, b)

  # See org.bluetooth.characteristic.gap.appearance.xml
  _append(_ADV_TYPE_APPEARANCE, struct.pack("<h", appearance))

  return payload

#Interrupt Request Handler
def bt_irq(event, data):
  # Track connections so we can send notifications.
  if event == _IRQ_CENTRAL_CONNECT:
    conn_handle, addr_type, addr, = data
  elif event == _IRQ_CENTRAL_DISCONNECT:
    conn_handle, addr_type, addr, = data
    # Start advertising again to allow a new connection.
    payload = advertising_payload(name=ble.config('gap_name'), appearance=_ADV_APPEARANCE_GENERIC_COMPUTER)
    ble.gap_advertise(500000, adv_data=payload)
    print("Advertised!")
  elif event == _IRQ_GATTS_WRITE:
    conn_handle, value_handle, = data
    processWrite(ble.gatts_read(rx))

print("Bluetooth Setup!")

#Bluetooth Setup
ble = ubluetooth.BLE()
ble.active(True)
ble.config(gap_name='Tactile Music')

#Nordic UART Setup straight from https://docs.micropython.org/en/latest/library/ubluetooth.html  (Although it's not necessarily Nordic)
#UUIDS ensured to be outside of Bluetooth's 16-bit standard (Not falling into xxxxxxxx-0000-1000-8000-00805F9B34FB)
UART_UUID = ubluetooth.UUID('b7328f9c-c89e-4d74-9a5e-000000000000')
UART_TX = (ubluetooth.UUID('b7328f9c-c89e-4d74-9a5e-000000000001'), ubluetooth.FLAG_READ | ubluetooth.FLAG_NOTIFY,)
UART_RX = (ubluetooth.UUID('b7328f9c-c89e-4d74-9a5e-000000000002'), ubluetooth.FLAG_WRITE,)
UART_SERVICE = (UART_UUID, (UART_TX, UART_RX,),)
ble.irq(handler=bt_irq)
((tx, rx,),) = ble.gatts_register_services((UART_SERVICE,))

#Advertise the connection!
payload = advertising_payload(name=ble.config('gap_name'), appearance=_ADV_APPEARANCE_GENERIC_COMPUTER)
ble.gap_advertise(500000, adv_data=payload)

ble.gatts_write(tx, "This is my secret message!".encode('UTF-8'))
print("READ: ", ble.gatts_read(tx).decode('UTF-8'))

#MAC is A4:CF:12:58:72:2A FROM ubinascii.hexlify(ble.conf('mac'),':').decode()
import ubinascii
print(ubinascii.hexlify(ble.config('mac'),':').decode())

#Endless Flashing
print("FADE START")

while True:
  up.duty(up_duty)
  left.duty(left_duty)
  down.duty(down_duty)
  right.duty(right_duty)
  sleep(0.05)





