#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"


#define LCD_WIDTH 128
#define LCD_HEIGHT 64
#define TICK_MS 40u
#define BOARD_WIDTH 15
#define BOARD_HEIGHT 6
#define CFG_MAGIC 0x5361u

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint32_t seed;
} config_t;

static const app_api_t *A;


static uint8_t board[BOARD_WIDTH][BOARD_HEIGHT];

#define FLAG_BIT (1 << 4)
#define MINE_BIT (1 << 5)
#define HIDDEN_BIT (1 << 6)

/*
Board tile bit layout
   Flag
   | Mine
   | | Hidden 
   | | |
   | | | 
   1 1 1 1000 - Mines around (0-8)
*/

static uint32_t randSeed, seed;
static bool isRunning, gameWin, gameLoss;
static uint8_t previousKey;
static uint16_t repeatMs;
static uint8_t mines_left, flags_left;
static char status_str[15];
static uint8_t cursor_x, cursor_y;
static uint8_t cursor_blink = 0;

static const uint8_t mine_sprite[8] =   { 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00, 0x00, 0x00 };
static const uint8_t flag_sprite[8] =   { 0x14, 0x16, 0x1F, 0x10, 0x10, 0x00, 0x00, 0x00 };
static const uint8_t cursor_sprite[8] = { 0x63, 0x41, 0x00, 0x00, 0x00, 0x41, 0x63, 0x00 };

typedef enum {
  NORMAL,
  SCARED,
  COOL,
  DEAD,
} smileyFace;

smileyFace currentFace = NORMAL;

static const uint8_t smiley_sprites[4][8] = {
  { 0x00, 0x10, 0x22, 0x20, 0x20, 0x22, 0x10, 0x00 }, // Normal
  { 0x00, 0x00, 0x32, 0x48, 0x48, 0x32, 0x00, 0x00 }, // Scared
  { 0x02, 0x16, 0x26, 0x22, 0x22, 0x26, 0x16, 0x02 }, // Cool
  { 0x05, 0x22, 0x15, 0x10, 0x10, 0x15, 0x22, 0x05 }, // Dead
};


static int rand_custom(void) {
  randSeed = randSeed * 1103515245u + 12345u;
  return (randSeed >> 16) & 0x7FFF; 
}

static void invertRect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
  for (uint8_t y = y1; y <= y2; y++) {
    for (uint8_t x = x1; x <= x2; x++) {
      A->fb[y/8][x] ^= (1 << (y % 8));
    }
  }
}

// Draw 8x8 sprite
static void drawSprite(uint8_t x, uint8_t y, const uint8_t* sprite) {
  uint8_t pixel_data;
  uint8_t bit_position = (y % 8);
  uint8_t page = y / 8;

  for (uint8_t byte = 0; byte < 8; byte++) {
    pixel_data = sprite[byte];

    A->fb[page][x + byte] |= (0xFF & (pixel_data << bit_position));

    if ((8 - bit_position) < 0) {
      A->fb[page+1][x + byte] |= (pixel_data >> (8 - bit_position));
    }
  }
}

/* zero-padded unsigned -> string, width w (<= 5). Replaces sprintf. */
static void u2str(char *out, const char *label, uint16_t v, uint8_t w) {
  char *o = out;
  if (label != NULL) {
    while (*label) *o++ = *label++;
  }
  char tmp[6]; int8_t n = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
  while (n < w) tmp[n++] = '0';
  while (n--) *o++ = tmp[n];
  *o = '\0';
}

static void generateMines(void) {
  mines_left = 0;
  flags_left = 0;

  for (uint8_t x = 0; x < BOARD_WIDTH; x++) {
    for (uint8_t y = 0; y < BOARD_HEIGHT; y++) {
      board[x][y] |= HIDDEN_BIT;

      if (x == 0 && y == 0) {continue;} 

      if (rand_custom() % 5 == 0) {
        board[x][y] |= MINE_BIT;
        mines_left++;
        flags_left++;
      } 
    }
  }

  // Calculate neighbouring mines
  for (uint8_t x = 0; x < BOARD_WIDTH; x++) {
  for (uint8_t y = 0; y < BOARD_HEIGHT; y++) {
    if (board[x][y] & MINE_BIT) {continue;}

    uint8_t mines_around = 0;

    for (int8_t nx = -1; nx <= 1; nx++) {
    for (int8_t ny = -1; ny <= 1; ny++) {

      if (nx == 0 && ny == 0) {continue;}

      if (x + nx < 0 || x + nx >= BOARD_WIDTH ||
          y + ny < 0 || y + ny >= BOARD_HEIGHT) {
        continue;
      }

      if (board[x + nx][y + ny] & MINE_BIT) {
        mines_around++;
      }
    }
    }

    board[x][y] |= mines_around;
  }
  }
}

static void initGame(void) {
  A->led(false);

  cursor_x = 0;
  cursor_y = 0;
  gameWin = false;
  gameLoss = false;
  currentFace = NORMAL;

  for (uint8_t x = 0; x < BOARD_WIDTH; x++) {
    for (uint8_t y = 0; y < BOARD_HEIGHT; y++) {
      board[x][y] = HIDDEN_BIT;
    }
  }

  generateMines();
}

static void drawField(void) {
  for (uint8_t x = 0; x < BOARD_WIDTH*8; x += 8) {
    for (uint8_t y = 0; y < BOARD_HEIGHT*8; y += 8) {
      uint8_t cur_tile = board[x/8][y/8];

      if (cur_tile & HIDDEN_BIT) {
        A->draw_rect(A->fb, x, y, x + 8, y + 8, true);
      }

      if (cur_tile & FLAG_BIT) {
        drawSprite(x + 2, y + 2, flag_sprite);

      } else if ((cur_tile & 0x0F) > 0 && !(cur_tile & HIDDEN_BIT)) {

        char num_str[1];
        u2str(num_str, NULL, (cur_tile & 0x0F), 1);
        A->print_tiny(num_str, x + 3, y + 2, false, true);
      }
    }
  }

  // Board edges
  A->draw_line(A->fb, 0, 0, 120, 0, true);
  A->draw_line(A->fb, 0, 0, 0, 48, true);

  A->draw_line(A->fb, 120, 0, 120, 48, true);
  A->draw_line(A->fb, 0, 48, 120, 48, true);

  A->draw_line(A->fb, 7, 55, 127, 55, true);
  A->draw_line(A->fb, 127, 7, 127, 55, true);
} 

static void draw3dEffect(void) {
  for (uint8_t x = 0; x < 7; x++) {
    A->fb[6][x]       |= (0b1 <<  (x % 8));
    A->fb[0][121 + x] |= (0b10 << (x % 8));
    A->fb[6][121 + x] |= (0b10 << (x % 8));
  }
}

static void drawCursor(void) {
  if (cursor_blink < 9) {
    drawSprite(cursor_x + 1, cursor_y + 1, cursor_sprite);
  }
}

static void drawStatus(void) {
  u2str(status_str, "FLAGS LEFT: ", flags_left, 2);
  A->print_tiny(status_str, 1, 1, true, true);

  for (uint8_t byte = 0; byte < 8; byte++) {
    A->status_line[65 + byte] |= smiley_sprites[currentFace][byte];
  }
}

static void renderLoss(void) {
  A->display_clear();
  A->status_clear();

  drawField();
  draw3dEffect();
  currentFace = DEAD;
  drawStatus();

  for (uint8_t x = 0; x < BOARD_WIDTH*8; x += 8) {
    for (uint8_t y = 0; y < BOARD_HEIGHT*8; y += 8) {

      if (board[x/8][y/8] & MINE_BIT) {
        // Reveal remaining mines
        if (!(board[x/8][y/8] & FLAG_BIT)) {
          drawSprite(x + 2, y + 2, mine_sprite);
        } 

      } else { 
        // Mark wrongly placed flags
        if (board[x/8][y/8] & FLAG_BIT) {
          invertRect(x + 1, y + 1, x + 7, y + 7);
        }
      }
    }
  }

  // Mark the hit mine 
  invertRect(cursor_x + 1, cursor_y + 1, cursor_x + 7, cursor_y + 7);

  u2str(status_str, "FLAGS LEFT: ", flags_left, 2);
  A->print_tiny(status_str, 1, 1, true, true);
  A->print_tiny("YOU LOST!", 80, 1, true, true);

  A->blit_status();
  A->blit_full();
}

static void renderWin(void) {
  A->led(true);
  drawField();
  draw3dEffect();
  currentFace = COOL;
  drawStatus();

  A->print_tiny("YOU WIN!", 80, 1, true, true);
  A->blit_status();
  A->blit_full();
}

static void toggleFlag(uint8_t x, uint8_t y) {
  if (!(board[x][y] & HIDDEN_BIT) || flags_left == 0) {return;}

  if (board[x][y] & FLAG_BIT) {
    flags_left++;
  } else {
    flags_left--;
  }

  if (board[x][y] & MINE_BIT) {
    mines_left--;
  } else {
    mines_left++;
  }

  board[x][y] ^= FLAG_BIT;
}

static void revealTiles(int8_t x, int8_t y) {
  if (x < 0 || x >= BOARD_WIDTH ||
      y < 0 || y >= BOARD_HEIGHT) {
    return;
  }

  // Skip if already revealed or flag placed
  if (!(board[x][y] & HIDDEN_BIT) || board[x][y] & FLAG_BIT) {
    return;
  } else {
    board[x][y] &= ~HIDDEN_BIT;
  }

  if (board[x][y] & MINE_BIT || board[x][y] & 0x0F) {
    return;
  }

  for (int8_t nx = -1; nx <= 1; nx++) {
    for (int8_t ny = -1; ny <= 1; ny++) {
      if (nx == 0 && ny == 0) {continue;}

      revealTiles(x + nx, y + ny);
    }
  }
}

static void key_action(uint8_t key, bool repeat) {
  if (key == APP_KEY_EXIT) {
    isRunning = false;
    return;
  }

  cursor_blink = 0;

  if (gameLoss) {
    renderLoss();

    if (!repeat && (key == APP_KEY_MENU || key == APP_KEY_STAR || key == APP_KEY_0)) {
      initGame();
    }
    return;

  } else if (gameWin) {
    renderWin();

    if (!repeat && (key == APP_KEY_MENU || key == APP_KEY_STAR || key == APP_KEY_0)) {
      initGame();
    }
    return;
  }

  if (key != APP_KEY_STAR) {
    currentFace = NORMAL;
  }

  int8_t dx = 0;
  int8_t dy = 0;

  if (key == APP_KEY_1) {          // UP LEFT
    dx = -8;
    dy = -8;
  } else if (key == APP_KEY_2) {   // UP
    dy = -8;
  } else if (key == APP_KEY_3) {   // UP RIGHT
    dx = 8;
    dy = -8;
  } else if (key == APP_KEY_4) {   // LEFT
    dx = -8;
  } else if (key == APP_KEY_6) {   // RIGHT
    dx = 8;
  } else if (key == APP_KEY_7) {   // DOWN LEFT
    dx = -8;
    dy = 8;
  } else if (key == APP_KEY_8) {   // DOWN
    dy = 8;
  } else if (key == APP_KEY_9) {   // DOWN RIGHT
    dx = 8;
    dy = 8;

  } else if (key == APP_KEY_F) { // Flagging
    toggleFlag(cursor_x / 8, cursor_y / 8);

    if (mines_left == 0) {
      gameWin = true;
    }

  } else if (key == APP_KEY_STAR) { // Revealing
    currentFace = SCARED;

    if (board[cursor_x / 8][cursor_y / 8] & MINE_BIT) {
      gameLoss = true; 
    } 

    revealTiles(cursor_x / 8, cursor_y / 8);
  }


  cursor_x = (cursor_x + dx + 120) % 120;
  cursor_y = (cursor_y + dy + 48) % 48;

  if (repeat) {return;}
}

void poll_key(void) {
  uint8_t key = A->get_key();

  if (key == APP_KEY_INVALID) {
    previousKey = key;
    repeatMs = 0;
    return;
  }
  if (key != previousKey) {
    key_action(key, false);
    repeatMs = 260u;
  } else if (repeatMs > TICK_MS) {
    repeatMs -= TICK_MS;
  } else {
    key_action(key, true);
    repeatMs = 80u;
  }
  previousKey = key;
}

/* ---- entry point, pinned to blob offset 0 ---- */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api) {
  A = api;
  config_t cfg;
  A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
  seed = cfg.magic == CFG_MAGIC && cfg.version == 1u ? cfg.seed : 1u;

  randSeed = ((uint32_t)A->bk_read(0x67u) << 16) ^ A->rx_freq() ^ seed;
  A->led(false);
  A->backlight_on();

  initGame();

  isRunning = true;

  while(isRunning) {
    poll_key();

    if (!gameWin && !gameLoss) {
      A->display_clear();
      A->status_clear();

      drawField();
      draw3dEffect();
      drawCursor();
      drawStatus();

      A->delay_ms(TICK_MS); 
      A->blit_status();
      A->blit_full();
    }

    cursor_blink++;
    if (cursor_blink >= 20) {cursor_blink = 0;}
  } 


  cfg.magic = CFG_MAGIC;
  cfg.version = 1u;
  cfg.seed = randSeed;
  A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
}
