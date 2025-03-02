from machine import Pin, SPI
import max7219
import random
from time import sleep_ms

DELAYS = {
  'slow': [500, 1000, 1500, 2000],
  'fast': [10, 25, 50, 100]
}
RELAX_MAX_TIME = 5000
FAST_MAX_TIME = 2500
FAST_CHANCE = 0.01

spi = SPI(0,sck=Pin(2),mosi=Pin(3))
cs = Pin(5, Pin.OUT)
display = max7219.Matrix8x8(spi, cs, 12)
display.brightness(1)

fast = False
time_fast = 0
relax_time = 0

while True:
  # Check whether we're in frantic mode
  if not fast and relax_time > RELAX_MAX_TIME:
    if random.random() <= FAST_CHANCE:
      fast = True
  elif fast and time_fast > FAST_MAX_TIME:
    fast = False
    time_fast = 0
    relax_time = 0

  # Do the algo
  for y in range (8):
    for x in range (96):
      flip = random.randint(0, 1)
      if (flip == 0):
        flipp = random.randint(0, 1)    
        if (flipp == 0):
          display.pixel(x,y,1)
        else:
          display.pixel(x,y,0)
      else:
        pass
  display.show()

  # Zzzzz
  time_sleep = random.choice(DELAYS['fast' if fast else 'slow'])
  if fast:
    time_fast += time_sleep
  else:
    relax_time += time_sleep

  sleep_ms(time_sleep)
