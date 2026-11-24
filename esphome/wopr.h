// 1U WOPR animation — C++ port of main.py / max7219.py for ESPHome.
// Keeps its own 96x8 framebuffer, because the max7219digit component clears
// its buffer before every lambda call.
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "esphome/core/color.h"
#include "esphome/core/helpers.h"

namespace wopr {

static const int WIDTH = 96;
static const int HEIGHT = 8;

// main.py defines update_random_mode() but never calls it, so the "fast"
// bursts of the random section never happen there. They are off by default
// here too; Controller::set_fast_bursts() enables them at runtime.
static const float FAST_CHANCE = 0.05f;
static const uint32_t FAST_RUN_TIME = 5000;
static const uint32_t RELAX_RUN_TIME = 5000;

static const uint32_t DELAYS_SLOW[4] = {500, 1000, 1500, 2000};
static const uint32_t DELAYS_FAST[4] = {20, 50, 100, 200};

// Nominal value from main.py. MicroPython on a Pico cannot really keep up with
// it; Controller::set_life_interval() changes it at runtime.
static const uint32_t DEFAULT_LIFE_INTERVAL = 50;
static const uint32_t LIFE_MAX_AGE = 60000;

struct Section {
  int x, y, w, h;
};

// TEMPORARY: the 2nd module from the left (x 8..15) has a faulty MAX7219 and
// its LED matrix is pulled out. With this set, program A and its counter move
// one module to the right and Life shrinks from 32 to 24 columns so nothing is
// drawn on the dead module. Set to false once the module is repaired to get
// the original layout back.
#define WOPR_SKIP_SECOND_MODULE true

static const Section SHIFTER = {0, 0, 8, 8};
#if WOPR_SKIP_SECOND_MODULE
static const Section PROGRAM_A = {18, 0, 16, 4};
static const Section A_COUNTER = {18, 6, 16, 2};
static const Section LIFE = {36, 0, 24, 8};
#else
static const Section PROGRAM_A = {10, 0, 16, 4};
static const Section A_COUNTER = {10, 6, 16, 2};
static const Section LIFE = {28, 0, 32, 8};
#endif
static const Section PROGRAM_B = {62, 0, 16, 4};
static const Section B_COUNTER = {62, 6, 16, 2};
static const Section RANDOM = {80, 0, 16, 8};

static const uint16_t SEED_SHIFTER[8] = {0xEC, 0x62, 0x38, 0x51, 0x36, 0x66, 0x59, 0x84};
static const uint16_t SEED_PROGRAM_A[4] = {0x7018, 0x1C61, 0x0C0C, 0x3039};
static const uint16_t SEED_PROGRAM_B[4] = {0x42AB, 0x0070, 0x06E2, 0x6064};

static const int LIFE_W = WOPR_SKIP_SECOND_MODULE ? 24 : 32;
static const int LIFE_H = 8;

class Controller {
 public:
  // Advances all sections. Returns true if anything changed.
  bool tick(uint32_t now) {
    bool changed = false;
    changed |= this->tick_shifter_(now);
    changed |= this->tick_program_a_(now);
    changed |= this->tick_a_counter_(now);
    changed |= this->tick_life_(now);
    changed |= this->tick_program_b_(now);
    changed |= this->tick_b_counter_(now);
    changed |= this->tick_random_(now);
    if (this->fast_bursts_)
      this->update_random_mode_(now);
    return changed;
  }

  void set_fast_bursts(bool enabled) {
    this->fast_bursts_ = enabled;
    if (!enabled)
      this->random_fast_ = false;
  }

  void set_life_interval(uint32_t ms) { this->life_interval_ = ms; }

  // Start Life over from a fresh random board on the next tick
  void reseed_life() { this->life_init_ = false; }

  template<typename D> void draw(D &it) {
    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        if (this->fb_[y][x])
          it.draw_pixel_at(x, y, esphome::Color::WHITE);
      }
    }
  }

 protected:
  static bool due_(uint32_t now, uint32_t at) { return (int32_t) (now - at) >= 0; }
  // Inclusive on both ends, like Python's random.randint
  static int rand_int_(int lo, int hi) { return lo + (int) (esphome::random_uint32() % (uint32_t) (hi - lo + 1)); }
  static bool rand_bit_() { return (esphome::random_uint32() >> 16) & 1; }

  void init_section_(const Section &s, const uint16_t *seed) {
    for (int y = 0; y < s.h; y++)
      for (int x = 0; x < s.w; x++)
        this->fb_[s.y + y][s.x + x] = (seed[y] >> x) & 1;
  }

  // Horizontal shift with wrap-around; positive = right, negative = left
  void shift_region_(int x0, int y0, int w, int h, int shift) {
    if (shift == 0)
      return;
    uint8_t tmp[WIDTH];
    for (int y = y0; y < y0 + h; y++) {
      memcpy(tmp, &this->fb_[y][x0], w);
      for (int i = 0; i < w; i++)
        this->fb_[y][x0 + i] = tmp[(((i - shift) % w) + w) % w];
    }
  }

  void draw_counter_(const Section &s, int64_t counter) {
    for (int y = 0; y < s.h; y++)
      for (int x = 0; x < s.w; x++)
        this->fb_[s.y + y][s.x + s.w - x - 1] = (counter >> (y * 4 + x)) & 1;
  }

  bool tick_shifter_(uint32_t now) {
    if (!due_(now, this->shifter_next_))
      return false;
    this->shifter_next_ = now + 700;

    if (!this->shifter_init_) {
      this->init_section_(SHIFTER, SEED_SHIFTER);
      this->shifter_init_ = true;
    }
    this->shift_region_(SHIFTER.x, SHIFTER.y, SHIFTER.w, SHIFTER.h, 1);
    return true;
  }

  // Normal shift is -1; 10% chance per tick to pause or reverse (1..3) for 300-1000 ms
  int program_a_shift_(uint32_t now, uint32_t &expiry, int &value) {
    if ((int32_t) (expiry - now) > 0)
      return value;
    if (esphome::random_float() < 0.1f) {
      value = esphome::random_float() < 0.5f ? 0 : rand_int_(1, 3);
      expiry = now + rand_int_(300, 1000);
      return value;
    }
    expiry = now;
    return -1;
  }

  bool tick_program_a_(uint32_t now) {
    if (!due_(now, this->program_a_next_))
      return false;
    this->program_a_next_ = now + 100;

    if (!this->program_a_init_) {
      this->init_section_(PROGRAM_A, SEED_PROGRAM_A);
      this->program_a_init_ = true;
    }

    const Section &s = PROGRAM_A;
    int half = s.h / 2;
    int upper = this->program_a_shift_(now, this->program_a_upper_expiry_, this->program_a_upper_value_);
    int lower = this->program_a_shift_(now, this->program_a_lower_expiry_, this->program_a_lower_value_);
    this->shift_region_(s.x, s.y, s.w, half, upper);
    this->shift_region_(s.x, s.y + half, s.w, s.h - half, lower);
    return true;
  }

  bool tick_a_counter_(uint32_t now) {
    if (!due_(now, this->a_counter_next_))
      return false;
    this->a_counter_next_ = now + 100;
    this->draw_counter_(A_COUNTER, now);
    return true;
  }

  void init_life_(uint32_t now) {
    for (int y = 0; y < LIFE_H; y++) {
      for (int x = 0; x < LIFE_W; x++) {
        this->life_[y][x] = rand_bit_();
        this->fb_[LIFE.y + y][LIFE.x + x] = this->life_[y][x];
      }
    }
    this->life_reset_time_ = now;
    this->life_has_prev_ = false;
    this->life_has_prev2_ = false;
    this->life_init_ = true;
  }

  bool tick_life_(uint32_t now) {
    if (!due_(now, this->life_next_))
      return false;
    this->life_next_ = now + this->life_interval_;

    if (!this->life_init_) {
      this->init_life_(now);
      return true;
    }

    uint8_t next[LIFE_H][LIFE_W];
    bool any_alive = false;
    for (int y = 0; y < LIFE_H; y++) {
      for (int x = 0; x < LIFE_W; x++) {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            if (dy == 0 && dx == 0)
              continue;
            n += this->life_[(y + dy + LIFE_H) % LIFE_H][(x + dx + LIFE_W) % LIFE_W];
          }
        }
        next[y][x] = this->life_[y][x] ? (n == 2 || n == 3) : (n == 3);
        any_alive |= next[y][x];
      }
    }

    // Reseed when the board is still, oscillates with period 2, died out, or is 60 s old
    bool stale = (this->life_has_prev_ && memcmp(next, this->life_prev_, sizeof(next)) == 0) ||
                 (this->life_has_prev2_ && memcmp(next, this->life_prev2_, sizeof(next)) == 0) || !any_alive ||
                 (now - this->life_reset_time_) >= LIFE_MAX_AGE;
    if (stale) {
      this->init_life_(now);
      return true;
    }

    for (int y = 0; y < LIFE_H; y++)
      memcpy(&this->fb_[LIFE.y + y][LIFE.x], next[y], LIFE_W);

    memcpy(this->life_prev2_, this->life_prev_, sizeof(next));
    this->life_has_prev2_ = this->life_has_prev_;
    memcpy(this->life_prev_, this->life_, sizeof(next));
    this->life_has_prev_ = true;
    memcpy(this->life_, next, sizeof(next));
    return true;
  }

  bool tick_program_b_(uint32_t now) {
    if (!due_(now, this->program_b_next_))
      return false;
    this->program_b_next_ = now + 150;

    if (!this->program_b_init_) {
      this->init_section_(PROGRAM_B, SEED_PROGRAM_B);
      this->program_b_init_ = true;
    }

    // Normal shift is 2; 10% chance per tick to pick -3..3 (never 0) for 1-3 s
    int shift;
    if ((int32_t) (this->program_b_expiry_ - now) > 0) {
      shift = this->program_b_value_;
    } else if (esphome::random_float() < 0.1f) {
      static const int CHOICES[6] = {-3, -2, -1, 1, 2, 3};
      shift = CHOICES[rand_int_(0, 5)];
      this->program_b_expiry_ = now + rand_int_(1000, 3000);
      this->program_b_value_ = shift;
    } else {
      shift = 2;
      this->program_b_expiry_ = now;
    }

    this->shift_region_(PROGRAM_B.x, PROGRAM_B.y, PROGRAM_B.w, PROGRAM_B.h, shift);
    return true;
  }

  bool tick_b_counter_(uint32_t now) {
    if (!due_(now, this->b_counter_next_))
      return false;
    this->b_counter_next_ = now + 200;

    if (due_(now, this->b_counter_next_reset_)) {
      this->b_counter_value_ = esphome::random_uint32();
      this->b_counter_next_reset_ = now + rand_int_(20, 60) * 1000;
    }

    this->draw_counter_(B_COUNTER, this->b_counter_value_);
    this->b_counter_value_--;
    return true;
  }

  bool tick_random_(uint32_t now) {
    if (!due_(now, this->random_next_))
      return false;
    const uint32_t *delays = this->random_fast_ ? DELAYS_FAST : DELAYS_SLOW;
    this->random_next_ = now + delays[rand_int_(0, 3)];

    for (int y = 0; y < RANDOM.h; y++)
      for (int x = 0; x < RANDOM.w; x++)
        if (rand_bit_())
          this->fb_[RANDOM.y + y][RANDOM.x + x] = rand_bit_();
    return true;
  }

  void update_random_mode_(uint32_t now) {
    if (!due_(now, this->random_mode_next_))
      return;
    if (this->random_fast_) {
      this->random_mode_next_ = now + RELAX_RUN_TIME;
      this->random_fast_ = false;
    } else {
      this->random_mode_next_ = now + FAST_RUN_TIME;
      if (esphome::random_float() <= FAST_CHANCE)
        this->random_fast_ = true;
    }
  }

  uint8_t fb_[HEIGHT][WIDTH] = {};

  uint32_t shifter_next_ = 0;
  bool shifter_init_ = false;

  uint32_t program_a_next_ = 0;
  bool program_a_init_ = false;
  uint32_t program_a_upper_expiry_ = 0;
  int program_a_upper_value_ = 0;
  uint32_t program_a_lower_expiry_ = 0;
  int program_a_lower_value_ = 0;

  uint32_t a_counter_next_ = 0;

  uint32_t life_next_ = 0;
  uint32_t life_interval_ = DEFAULT_LIFE_INTERVAL;
  bool life_init_ = false;
  bool life_has_prev_ = false;
  bool life_has_prev2_ = false;
  uint32_t life_reset_time_ = 0;
  uint8_t life_[LIFE_H][LIFE_W] = {};
  uint8_t life_prev_[LIFE_H][LIFE_W] = {};
  uint8_t life_prev2_[LIFE_H][LIFE_W] = {};

  uint32_t program_b_next_ = 0;
  bool program_b_init_ = false;
  uint32_t program_b_expiry_ = 0;
  int program_b_value_ = 0;

  uint32_t b_counter_next_ = 0;
  uint32_t b_counter_next_reset_ = 0;
  int64_t b_counter_value_ = 0;

  uint32_t random_next_ = 0;
  uint32_t random_mode_next_ = 0;
  bool random_fast_ = false;
  bool fast_bursts_ = false;
};

// Single shared instance, so the display lambda and HA entities reach the same animation
inline Controller &controller() {
  static Controller instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Scrolling text
// ---------------------------------------------------------------------------

// Classic 5x7 font, ASCII 32..126, five columns per glyph, bit 0 = top row
static const uint8_t FONT_5X7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00},  // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00},  // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
    {0x00, 0x01, 0x02, 0x04, 0x00},  // `
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x20},  // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},  // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E},  // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // j
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},  // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0x7C, 0x14, 0x14, 0x14, 0x08},  // p
    {0x08, 0x14, 0x14, 0x18, 0x7C},  // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x20},  // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},  // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},  // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C},  // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},  // z
    {0x00, 0x08, 0x36, 0x41, 0x00},  // {
    {0x00, 0x00, 0x7F, 0x00, 0x00},  // |
    {0x00, 0x41, 0x36, 0x08, 0x00},  // }
    {0x08, 0x04, 0x08, 0x10, 0x08},  // ~
};

static const int GLYPH_ADVANCE = 6;          // 5 columns + 1 column gap
static const uint32_t SCROLL_STEP_MS = 30;   // time per pixel
static const size_t SCROLL_MAX_QUEUE = 8;
static const size_t SCROLL_MAX_CHARS = 255;

// Scrolls queued messages right-to-left across the whole display, one after
// another. While a message is on screen it replaces the animation.
class TextScroller {
 public:
  void show(const std::string &text) {
    if (this->queue_.size() >= SCROLL_MAX_QUEUE)
      return;
    std::vector<uint8_t> glyphs = to_glyphs_(text);
    if (!glyphs.empty())
      this->queue_.push_back(std::move(glyphs));
  }

  void clear() {
    this->queue_.clear();
    this->current_.clear();
  }

  bool active() const { return !this->current_.empty() || !this->queue_.empty(); }

  void tick(uint32_t now) {
    if (this->current_.empty()) {
      if (this->queue_.empty())
        return;
      this->current_ = std::move(this->queue_.front());
      this->queue_.pop_front();
      this->offset_ = 0;
      this->next_step_ = now + SCROLL_STEP_MS;
      return;
    }
    while ((int32_t) (now - this->next_step_) >= 0) {
      this->next_step_ += SCROLL_STEP_MS;
      this->offset_++;
      // Done once the last glyph has left the left edge
      if (this->offset_ > WIDTH + (int) this->current_.size() * GLYPH_ADVANCE) {
        this->current_.clear();
        return;
      }
    }
  }

  template<typename D> void draw(D &it) {
    for (size_t i = 0; i < this->current_.size(); i++) {
      int x0 = WIDTH - this->offset_ + (int) i * GLYPH_ADVANCE;
      if (x0 >= WIDTH || x0 + 5 <= 0)
        continue;
      const uint8_t *glyph = FONT_5X7[this->current_[i]];
      for (int c = 0; c < 5; c++) {
        int x = x0 + c;
        if (x < 0 || x >= WIDTH)
          continue;
        for (int y = 0; y < 7; y++)
          if ((glyph[c] >> y) & 1)
            it.draw_pixel_at(x, y, esphome::Color::WHITE);
      }
    }
  }

 protected:
  // UTF-8 text -> font indices. Polish letters lose their diacritics, anything
  // else outside printable ASCII becomes '?'.
  static std::vector<uint8_t> to_glyphs_(const std::string &text) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < text.size() && out.size() < SCROLL_MAX_CHARS) {
      uint8_t b = text[i];
      char ch = '?';
      if (b < 0x80) {
        i++;
        if (b < 32 || b > 126)
          continue;
        ch = (char) b;
      } else if ((b & 0xE0) == 0xC0 && i + 1 < text.size()) {
        uint32_t cp = ((b & 0x1F) << 6) | (text[i + 1] & 0x3F);
        i += 2;
        switch (cp) {
          case 0x0104: ch = 'A'; break;
          case 0x0105: ch = 'a'; break;
          case 0x0106: ch = 'C'; break;
          case 0x0107: ch = 'c'; break;
          case 0x0118: ch = 'E'; break;
          case 0x0119: ch = 'e'; break;
          case 0x0141: ch = 'L'; break;
          case 0x0142: ch = 'l'; break;
          case 0x0143: ch = 'N'; break;
          case 0x0144: ch = 'n'; break;
          case 0x00D3: ch = 'O'; break;
          case 0x00F3: ch = 'o'; break;
          case 0x015A: ch = 'S'; break;
          case 0x015B: ch = 's'; break;
          case 0x0179: case 0x017B: ch = 'Z'; break;
          case 0x017A: case 0x017C: ch = 'z'; break;
          default: break;
        }
      } else {
        // 3/4-byte sequence or stray byte: one '?' for the whole character
        i++;
        while (i < text.size() && (text[i] & 0xC0) == 0x80)
          i++;
      }
      out.push_back((uint8_t) (ch - 32));
    }
    return out;
  }

  std::deque<std::vector<uint8_t>> queue_;
  std::vector<uint8_t> current_;
  int offset_ = 0;
  uint32_t next_step_ = 0;
};

inline TextScroller &scroller() {
  static TextScroller instance;
  return instance;
}

}  // namespace wopr
