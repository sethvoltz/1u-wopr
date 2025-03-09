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
  SECTIONS = {
    'shifter':   { 'x':  0, 'y': 0, 'w':  8, 'h': 8 },
    'program-a': { 'x': 10, 'y': 0, 'w': 16, 'h': 4 },
    'a-counter': { 'x': 10, 'y': 6, 'w': 16, 'h': 2 },
    'life':      { 'x': 28, 'y': 0, 'w': 32, 'h': 8 },
    'program-b': { 'x': 62, 'y': 0, 'w': 16, 'h': 4 },
    'b-counter': { 'x': 62, 'y': 6, 'w': 16, 'h': 2 },
    'random':    { 'x': 80, 'y': 0, 'w': 16, 'h': 8 },
  }

  SEEDS = {
    'shifter':   [ 0xEC, 0x62, 0x38, 0x51, 0x36, 0x66, 0x59, 0x84 ],
    'program-a': [ 0x7018, 0x1C61, 0x0C0C, 0x3039 ],
    'program-b': [ 0x42AB, 0x0070, 0x06E2, 0x6064 ],
  }

  def __init__(self, display):
    self.section_ticks = [
      self.tick_section_shifter,
      self.tick_section_program_a,
      self.tick_section_a_counter,
      self.tick_section_program_b,
      self.tick_section_b_counter,
      self.tick_section_random,
    ]

    self.display = display
    self.random_mode_fast = False
    self.shifter_next_update = 0
    self.section_shifter_init = False
    self.program_a_next_update = 0
    self.section_program_a_init = False
    self.a_counter_next_update = 0
    self.program_b_next_update = 0
    self.b_counter_next_update = 0
    self.b_counter_next_reset = 0
    self.b_counter_value = 0
    self.section_program_b_init = False
    self.random_mode_next_update = 0
    self.random_next_update = 0


  def init_section(self, section, seed):
    for y in range(section['h']):
      for x in range(section['w']):
        self.display.pixel(
          section['x'] + x,
          section['y'] + y,
          (seed[y] >> x) & 1
        )

  def tick_section_shifter(self):
    if time.ticks_diff(time.ticks_ms(), self.shifter_next_update) < 0:
      return False
    
    self.shifter_next_update = time.ticks_add(time.ticks_ms(), 700)

    if not self.section_shifter_init:
      self.init_section(self.SECTIONS['shifter'], self.SEEDS['shifter'])
      self.section_shifter_init = True
    
    self.display.shift_region(
      self.SECTIONS['shifter']['x'],
      self.SECTIONS['shifter']['y'],
      self.SECTIONS['shifter']['w'],
      self.SECTIONS['shifter']['h'],
      1,
      True
    )

    return True
  
  def tick_section_program_a(self):
    if time.ticks_diff(time.ticks_ms(), self.program_a_next_update) < 0:
      return False
    
    self.program_a_next_update = time.ticks_add(time.ticks_ms(), 100)

    if not self.section_program_a_init:
      self.init_section(self.SECTIONS['program-a'], self.SEEDS['program-a'])
      self.section_program_a_init = True
    
    self.display.shift_region(
      self.SECTIONS['program-a']['x'],
      self.SECTIONS['program-a']['y'],
      self.SECTIONS['program-a']['w'],
      self.SECTIONS['program-a']['h'],
      -1,
      True
    )

    return True

  def tick_section_a_counter(self):
    if time.ticks_diff(time.ticks_ms(), self.a_counter_next_update) < 0:
      return False
    
    self.a_counter_next_update = time.ticks_add(time.ticks_ms(), 100)

    # this function represents the counter value of time.ticks_ms and refreshes every 100ms
    # the counter is displayed in the 'a-counter' section
    counter = time.ticks_ms()
    for y in range(self.SECTIONS['a-counter']['h']):
      for x in range(self.SECTIONS['a-counter']['w']):
        self.display.pixel(
          self.SECTIONS['a-counter']['x'] + self.SECTIONS['a-counter']['w'] - x - 1,
          self.SECTIONS['a-counter']['y'] + y,
          (counter >> (y * 4 + x)) & 1
        )
    
    return True

  def tick_section_program_b(self):
    if time.ticks_diff(time.ticks_ms(), self.program_b_next_update) < 0:
      return False
    
    self.program_b_next_update = time.ticks_add(time.ticks_ms(), 150)

    if not self.section_program_b_init:
      self.init_section(self.SECTIONS['program-b'], self.SEEDS['program-b'])
      self.section_program_b_init = True
    
    self.display.shift_region(
      self.SECTIONS['program-b']['x'],
      self.SECTIONS['program-b']['y'],
      self.SECTIONS['program-b']['w'],
      self.SECTIONS['program-b']['h'],
      2,
      True
    )

    return True

  def tick_section_b_counter(self):
    if time.ticks_diff(time.ticks_ms(), self.b_counter_next_update) < 0:
      return False

    self.b_counter_next_update = time.ticks_add(time.ticks_ms(), 200)

    if time.ticks_diff(time.ticks_ms(), self.b_counter_next_reset) >= 0:
      self.b_counter_value = random.getrandbits(32)
      duration = random.randint(20, 60) * 1000
      self.b_counter_next_reset = time.ticks_add(time.ticks_ms(), duration)

    counter = self.b_counter_value
    self.b_counter_value -= 1

    for y in range(self.SECTIONS['b-counter']['h']):
      for x in range(self.SECTIONS['b-counter']['w']):
        self.display.pixel(
          self.SECTIONS['b-counter']['x'] + self.SECTIONS['b-counter']['w'] - x - 1,
          self.SECTIONS['b-counter']['y'] + y,
          (counter >> (y * 4 + x)) & 1
        )

    return True

  def tick_section_random(self):
    if time.ticks_diff(time.ticks_ms(), self.random_next_update) < 0:
      return False

    delay = random.choice(DELAYS['fast' if self.random_mode_fast else 'slow'])
    self.random_next_update = time.ticks_add(time.ticks_ms(), delay)

    for y in range(self.SECTIONS['random']['h']):
      for x in range(self.SECTIONS['random']['w']):
        if random.randint(0, 1) == 0:
          self.display.pixel(
            self.SECTIONS['random']['x'] + x,
            self.SECTIONS['random']['y'] + y,
            random.randint(0, 1)
          )

    return True

  def update_random_mode(self):
    if time.ticks_diff(time.ticks_ms(), controller.random_mode_next_update) >= 0:
      if self.random_mode_fast:
        self.random_mode_next_update = time.ticks_add(time.ticks_ms(), RELAX_RUN_TIME)
        self.random_mode_fast = False
      else:
        self.random_mode_next_update = time.ticks_add(time.ticks_ms(), FAST_RUN_TIME)
        if random.random() <= FAST_CHANCE:
          self.random_mode_fast = True

  def run(self):
    while True:
      should_update_display = False

      for tick in self.section_ticks:
        should_update_display |= tick()

      if should_update_display:
        self.display.show()

controller = WOPRController(display)
controller.run()
