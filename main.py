from machine import Pin, SPI
import max7219
import random
import time

DELAYS = {
  'slow': [500, 1000, 1500, 2000],
  'fast': [10, 25, 50, 100]
}

FAST_CHANCE = 0.1
FAST_RUN_TIME = 5000
RELAX_RUN_TIME = 5000

spi = SPI(0,sck=Pin(2),mosi=Pin(3))
cs = Pin(5, Pin.OUT)
display = max7219.Matrix8x8(spi, cs, 12)
display.brightness(1)

class WOPRController:
  def __init__(self, display):
    self.display = display
    self.fast = False
    self.mode_update_time = 0
    self.display_update_time = 0

  def update_mode(self):
    if time.ticks_diff(time.ticks_ms(), controller.mode_update_time) >= 0:
      self.run_mode()

  def run_mode(self):
    if self.fast:
      self.mode_update_time = time.ticks_add(time.ticks_ms(), RELAX_RUN_TIME)
      self.fast = False
    else:
      self.mode_update_time = time.ticks_add(time.ticks_ms(), FAST_RUN_TIME)
      if random.random() <= FAST_CHANCE:
        self.fast = True

  def update_display(self):
    if time.ticks_diff(time.ticks_ms(), self.display_update_time) >= 0:
      self.run_display()

  def run_display(self):
    delay = random.choice(DELAYS['fast' if self.fast else 'slow'])
    self.display_update_time = time.ticks_add(time.ticks_ms(), delay)

    for y in range(8):
      for x in range(96):
        flip = random.randint(0, 1)
        if flip == 0:
          flipp = random.randint(0, 1)
          if flipp == 0:
            self.display.pixel(x, y, 1)
          else:
            self.display.pixel(x, y, 0)

    self.display.show()

  def run(self):
    while True:
      self.update_mode()
      self.update_display()

controller = WOPRController(display)
controller.run()
