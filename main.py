from machine import Pin, SPI
import max7219
import random
import time

DELAYS = {
  'slow': [500, 1000, 1500, 2000],
  'fast': [20, 50, 100, 200]
}

FAST_CHANCE = 0.05
FAST_RUN_TIME = 5000
RELAX_RUN_TIME = 5000

spi = SPI(0,sck=Pin(2),mosi=Pin(3))
cs = Pin(5, Pin.OUT)
display = max7219.Matrix8x8(spi, cs, 12)
display.brightness(1)

class WOPRController:
  def __init__(self, display):
    self.section_ticks = [self.tick_section_random]

    self.display = display
    self.fast = False
    self.mode_update_time = 0
    self.display_update_time = 0

  # TODO: Split the display into a bunch of regions that have different shift timing and direction
  # self.display.shift_region(8, 0, 16, 8, 1, True)
  # self.display.shift_region(32, 0, 24, 8, -2, True)

  def tick_section_random(self):
    if time.ticks_diff(time.ticks_ms(), self.display_update_time) < 0:
      return False

    delay = random.choice(DELAYS['fast' if self.fast else 'slow'])
    self.display_update_time = time.ticks_add(time.ticks_ms(), delay)

    for y in range(8):
      for x in range(16):
        if random.randint(0, 1) == 0:
          self.display.pixel(80 + x, y, random.randint(0, 1))

    return True

  def update_random_mode(self):
    if time.ticks_diff(time.ticks_ms(), controller.mode_update_time) >= 0:
      if self.fast:
        self.mode_update_time = time.ticks_add(time.ticks_ms(), RELAX_RUN_TIME)
        self.fast = False
      else:
        self.mode_update_time = time.ticks_add(time.ticks_ms(), FAST_RUN_TIME)
        if random.random() <= FAST_CHANCE:
          self.fast = True

  def run(self):
    while True:
      should_update_display = False

      for tick in self.section_ticks:
        should_update_display |= tick()

      if should_update_display:
        self.display.show()

controller = WOPRController(display)
controller.run()
