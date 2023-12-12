/**
 * MinInvaders -- Mini Space Invaders game
 * by Levente Loffler
 */

#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define __AVR_ATmega128__ 1
#include <avr/io.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// GENERAL INIT - USED BY ALMOST EVERYTHING ----------------------------------

static void port_init() {
  PORTA = 0b00011111;
  DDRA = 0b01000000;  // buttons & led
  PORTB = 0b00000000;
  DDRB = 0b00000000;
  PORTC = 0b00000000;
  DDRC = 0b11110111;  // lcd
  PORTD = 0b11000000;
  DDRD = 0b00001000;
  PORTE = 0b00100000;
  DDRE = 0b00110000;  // buzzer
  PORTF = 0b00000000;
  DDRF = 0b00000000;
  PORTG = 0b00000000;
  DDRG = 0b00000000;
}

// TIMER-BASED RANDOM NUMBER GENERATOR ---------------------------------------

static void rnd_init() {
  TCCR0 |= (1 << CS00);  // Timer 0 no prescaling (@FCPU)
  TCNT0 = 0;             // init counter
}

// LCD HELPERS ---------------------------------------------------------------

#define CLR_DISP 0x00000001
#define DISP_ON 0x0000000C
#define DISP_OFF 0x00000008
#define CUR_HOME 0x00000002
#define CUR_OFF 0x0000000C
#define CUR_ON_UNDER 0x0000000E
#define CUR_ON_BLINK 0x0000000F
#define CUR_LEFT 0x00000010
#define CUR_RIGHT 0x00000014
#define CG_RAM_ADDR 0x00000040
#define DD_RAM_ADDR 0x00000080
#define DD_RAM_ADDR2 0x000000C0

// #define		ENTRY_INC	    0x00000007	//LCD increment
// #define		ENTRY_DEC	    0x00000005	//LCD decrement
// #define		SH_LCD_LEFT	  0x00000010	//LCD shift left
// #define		SH_LCD_RIGHT	0x00000014	//LCD shift right
// #define		MV_LCD_LEFT	  0x00000018	//LCD move left
// #define		MV_LCD_RIGHT	0x0000001C	//LCD move right

static void lcd_delay(unsigned int b) {
  volatile unsigned int a = b;
  while (a) a--;
}

static void lcd_pulse() {
  PORTC = PORTC | 0b00000100;  // set E to high
  lcd_delay(1400);             // delay ~110ms
  PORTC = PORTC & 0b11111011;  // set E to low
}

static void lcd_send(int command, unsigned char a) {
  unsigned char data;

  data = 0b00001111 | a;                // get high 4 bits
  PORTC = (PORTC | 0b11110000) & data;  // set D4-D7
  if (command)
    PORTC =
        PORTC & 0b11111110;  // set RS port to 0 -> display set to command mode
  else
    PORTC = PORTC | 0b00000001;  // set RS port to 1 -> display set to data mode
  lcd_pulse();                   // pulse to set D4-D7 bits

  data = a << 4;                        // get low 4 bits
  PORTC = (PORTC & 0b00001111) | data;  // set D4-D7
  if (command)
    PORTC =
        PORTC & 0b11111110;  // set RS port to 0 -> display set to command mode
  else
    PORTC = PORTC | 0b00000001;  // set RS port to 1 -> display set to data mode
  lcd_pulse();                   // pulse to set d4-d7 bits
}

static void lcd_send_command(unsigned char a) { lcd_send(1, a); }

static void lcd_send_data(unsigned char a) { lcd_send(0, a); }

static void lcd_init() {
  // LCD initialization
  // step by step (from Gosho) - from DATASHEET

  PORTC = PORTC & 0b11111110;

  lcd_delay(10000);

  PORTC = 0b00110000;  // set D4, D5 port to 1
  lcd_pulse();         // high->low to E port (pulse)
  lcd_delay(1000);

  PORTC = 0b00110000;  // set D4, D5 port to 1
  lcd_pulse();         // high->low to E port (pulse)
  lcd_delay(1000);

  PORTC = 0b00110000;  // set D4, D5 port to 1
  lcd_pulse();         // high->low to E port (pulse)
  lcd_delay(1000);

  PORTC = 0b00100000;  // set D4 to 0, D5 port to 1
  lcd_pulse();         // high->low to E port (pulse)

  lcd_send_command(
      0x28);  // function set: 4 bits interface, 2 display lines, 5x8 font
  lcd_send_command(DISP_OFF);  // display off, cursor off, blinking off
  lcd_send_command(CLR_DISP);  // clear display
  lcd_send_command(
      0x06);  // entry mode set: cursor increments, display does not shift

  lcd_send_command(DISP_ON);   // Turn ON Display
  lcd_send_command(CLR_DISP);  // Clear Display
}

static void lcd_send_text(char *str) {
  while (*str) lcd_send_data(*str++);
}

static void lcd_send_line1(char *str) {
  lcd_send_command(DD_RAM_ADDR);
  lcd_send_text(str);
}

static void lcd_send_line2(char *str) {
  lcd_send_command(DD_RAM_ADDR2);
  lcd_send_text(str);
}

// MININVADERS ---------------------------------------------------------------

#define SCREEN_CH_WIDTH 16
#define SCREEN_CH_HEIGHT 2
#define SCREEN
#define CHAR_WIDTH 5
#define CHAR_HEIGHT 8
#define SCREEN_PX_WIDTH SCREEN_CH_WIDTH *CHAR_WIDTH
#define SCREEN_PX_HEIGHT SCREEN_CH_HEIGHT *CHAR_HEIGHT

#define EMPTY_LINE "                "

#define STARTING_PROJECTILE_H_POS 5

#define CANNON_CG_IDX 0
#define CANNON_PROJ_CG_IDX 1
#define INVADER_PROJ_CG_IDX 2

#define MAX_INVADER_Y 4
#define MAX_INVADER_X 15
#define MIN_INVADER_X 1
#define START_INVADER_X 6

#define DOUBLE_INVADER_CG_IDX 3
#define TOP_ONLY_INVADER_CG_IDX 4
#define BOT_ONLY_INVADER_CG_IDX 5
#define PROJ_INVADER_COMB_CG_IDX 6

typedef enum {
  BUTTON1 = 0,
  BUTTON2 = 1,
  BUTTON3 = 2,
  BUTTON4 = 3,
  BUTTON5 = 4
} Button;

typedef struct {
  int8_t x;
  int8_t y;
} Point;

typedef enum {
  INVADER_DIRECTION_UP,
  INVADER_DIRECTION_DOWN,
  INVADER_DIRECTION_SIDE_FROM_UP,
  INVADER_DIRECTION_SIDE_FROM_DOWN
} InvaderDirection;

typedef enum {
  INVADER_CONFIG_FLAG_NONE = 0x0,
  INVADER_CONFIG_FLAG_TOP = 0x1,
  INVADER_CONFIG_FLAG_BOT = 0x2,
} InvaderConfigFlags;

typedef struct {
  Point pxPos;
  bool isActive;
} CannonProjectileData;

static uint8_t const INVADER_SPRITES[][CHAR_HEIGHT] = {{
                                                           0b01101,
                                                           0b00010,
                                                           0b01101,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                       },
                                                       {
                                                           0b01010,
                                                           0b00110,
                                                           0b01010,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                           0b00000,
                                                       }};

static bool is_button_down(Button const button) {
  return ~PINA & 0x1 << button;
}

static int8_t clamp(int8_t const val, int8_t const min, int8_t const max) {
  if (val < min) {
    return min;
  }

  if (val > max) {
    return max;
  }

  return val;
}

static int8_t max(int8_t const val1, int8_t const val2) {
  return val1 > val2 ? val1 : val2;
}

static bool is_cannon_proj_active(CannonProjectileData const *const data) {
  return data->isActive;
}

static void set_cannon_projectile_active(CannonProjectileData *const projData,
                                         int8_t const vPos) {
  projData->isActive = true;
  projData->pxPos.x = STARTING_PROJECTILE_H_POS;
  projData->pxPos.y = vPos;
}

static void set_cannon_projectile_inactive(CannonProjectileData *const data) {
  data->isActive = false;
}

static bool is_cannon_projectile_out(CannonProjectileData const *const data) {
  return data->pxPos.x >= SCREEN_PX_WIDTH;
}

static uint8_t get_dd_row_addr_from_px_y(int8_t const yPx) {
  return yPx >= SCREEN_PX_HEIGHT / 2 ? DD_RAM_ADDR2 : DD_RAM_ADDR;
}

static int8_t find_first_zero(uint8_t const *const arr, int8_t const sz) {
  for (int i = 0; i < sz; i++) {
    if (arr[i] == 0) {
      return i;
    }
  }
  return -1;
}

static InvaderDirection get_next_invader_direction(InvaderDirection const dir) {
  switch (dir) {
    case INVADER_DIRECTION_UP:
      return INVADER_DIRECTION_SIDE_FROM_UP;
    case INVADER_DIRECTION_DOWN:
      return INVADER_DIRECTION_SIDE_FROM_DOWN;
    case INVADER_DIRECTION_SIDE_FROM_UP:
      return INVADER_DIRECTION_DOWN;
    case INVADER_DIRECTION_SIDE_FROM_DOWN:
      return INVADER_DIRECTION_UP;
    default:
      return INVADER_DIRECTION_SIDE_FROM_DOWN;
  }
}

static void update_sprites_in_cg(int8_t const spriteIdx,
                                 int8_t const heightOffset,
                                 int8_t const invaderSpriteHeight) {
  lcd_send_command(CG_RAM_ADDR + DOUBLE_INVADER_CG_IDX * CHAR_HEIGHT);
  for (int i = 0; i < CHAR_HEIGHT; i++) {
    lcd_send_data(0);
  }

  lcd_send_command(CG_RAM_ADDR + TOP_ONLY_INVADER_CG_IDX * CHAR_HEIGHT);
  for (int i = 0; i < CHAR_HEIGHT; i++) {
    lcd_send_data(0);
  }

  lcd_send_command(CG_RAM_ADDR + BOT_ONLY_INVADER_CG_IDX * CHAR_HEIGHT);
  for (int i = 0; i < CHAR_HEIGHT; i++) {
    lcd_send_data(0);
  }

  lcd_send_command(CG_RAM_ADDR + DOUBLE_INVADER_CG_IDX * CHAR_HEIGHT +
                   heightOffset);
  for (int i = 0; i < invaderSpriteHeight; i++) {
    lcd_send_data(INVADER_SPRITES[spriteIdx][i]);
  }

  lcd_send_command(CG_RAM_ADDR + DOUBLE_INVADER_CG_IDX * CHAR_HEIGHT +
                   invaderSpriteHeight + 1 + heightOffset);
  for (int i = 0; i < invaderSpriteHeight; i++) {
    lcd_send_data(INVADER_SPRITES[spriteIdx][i]);
  }

  lcd_send_command(CG_RAM_ADDR + TOP_ONLY_INVADER_CG_IDX * CHAR_HEIGHT +
                   heightOffset);
  for (int i = 0; i < invaderSpriteHeight; i++) {
    lcd_send_data(INVADER_SPRITES[spriteIdx][i]);
  }

  lcd_send_command(CG_RAM_ADDR + BOT_ONLY_INVADER_CG_IDX * CHAR_HEIGHT +
                   invaderSpriteHeight + 1 + heightOffset);
  for (int i = 0; i < invaderSpriteHeight; i++) {
    lcd_send_data(INVADER_SPRITES[spriteIdx][i]);
  }
}

static void update_sprites_in_dd(
    InvaderConfigFlags const configPerScreenChar[SCREEN_CH_HEIGHT]
                                                [SCREEN_CH_WIDTH],
    int8_t const updateMinX) {
  for (int i = 0; i < SCREEN_CH_HEIGHT; i++) {
    int const rowAddr = i == 0 ? DD_RAM_ADDR : DD_RAM_ADDR2;

    for (int j = updateMinX; j < SCREEN_CH_WIDTH; j++) {
      lcd_send_command(rowAddr + j);

      bool const hasTop =
          (configPerScreenChar[i][j] & INVADER_CONFIG_FLAG_TOP) != 0;
      bool const hasBot =
          (configPerScreenChar[i][j] & INVADER_CONFIG_FLAG_BOT) != 0;

      if (hasTop && hasBot) {
        lcd_send_data(DOUBLE_INVADER_CG_IDX);
      } else if (hasTop) {
        lcd_send_data(TOP_ONLY_INVADER_CG_IDX);
      } else if (hasBot) {
        lcd_send_data(BOT_ONLY_INVADER_CG_IDX);
      } else {
        lcd_send_data(' ');
      }
    }
  }
}

static void shift_sprites_left_in_dd(
    InvaderConfigFlags configPerScreenChar[SCREEN_CH_HEIGHT][SCREEN_CH_WIDTH]) {
  for (int i = 0; i < SCREEN_CH_HEIGHT; i++) {
    for (int j = 0; j < SCREEN_CH_WIDTH - 1; j++) {
      configPerScreenChar[i][j] = configPerScreenChar[i][j + 1];
    }
  }

  configPerScreenChar[0][SCREEN_CH_WIDTH - 1] = INVADER_CONFIG_FLAG_NONE;
  configPerScreenChar[1][SCREEN_CH_WIDTH - 1] = INVADER_CONFIG_FLAG_NONE;
}

static int8_t recalculate_invader_start_x(
    InvaderConfigFlags const configFlags[SCREEN_CH_HEIGHT][SCREEN_CH_WIDTH]) {
  for (int j = 0; j < SCREEN_CH_WIDTH; j++) {
    bool colHasInvader = false;

    for (int i = 0; i < SCREEN_CH_HEIGHT; i++) {
      colHasInvader =
          colHasInvader || configFlags[i][j] != INVADER_CONFIG_FLAG_NONE;
    }

    if (colHasInvader) {
      return j;
    }
  }

  return -1;  // ???
}

static int8_t calculate_invader_sprite_height(int8_t const invaderSpriteIdx) {
  return find_first_zero(INVADER_SPRITES[invaderSpriteIdx], CHAR_HEIGHT);
}

static void get_sprite_cg_char_from_config(uint8_t dstBytes[CHAR_HEIGHT],
                                           InvaderConfigFlags const configFlags,
                                           int8_t const spriteIdx,
                                           int8_t const spriteHeight,
                                           int8_t const spriteYOffset) {
  if ((configFlags & INVADER_CONFIG_FLAG_TOP) != INVADER_CONFIG_FLAG_NONE) {
    memcpy(dstBytes + spriteYOffset, INVADER_SPRITES[spriteIdx], spriteHeight);
  }
  if ((configFlags & INVADER_CONFIG_FLAG_BOT) != INVADER_CONFIG_FLAG_NONE) {
    memcpy(dstBytes + spriteYOffset + spriteHeight + 1,
           INVADER_SPRITES[spriteIdx], spriteHeight);
  }
}

static uint8_t get_dd_value_from_config(InvaderConfigFlags const configFlags) {
  bool const hasTop = configFlags & INVADER_CONFIG_FLAG_TOP;
  bool const hasBot = configFlags & INVADER_CONFIG_FLAG_BOT;
  return hasTop && hasBot ? DOUBLE_INVADER_CG_IDX
         : hasTop         ? TOP_ONLY_INVADER_CG_IDX
         : hasBot         ? BOT_ONLY_INVADER_CG_IDX
                          : ' ';
}

int main() {
  port_init();
  lcd_init();
  rnd_init();

  lcd_send_line1("  MinInvaders   ");
  lcd_send_line2(" Press a button ");

  while (true) {
    if (is_button_down(BUTTON1) || is_button_down(BUTTON2) ||
        is_button_down(BUTTON3) || is_button_down(BUTTON4) ||
        is_button_down(BUTTON5)) {
      lcd_send_line1(EMPTY_LINE);
      lcd_send_line2(EMPTY_LINE);
      break;
    }
  }

  while (true) {
    InvaderDirection currentInvaderDir = INVADER_DIRECTION_DOWN;
    uint8_t invaderUpdateCycles = 0;
    int8_t currentInvaderStartX = START_INVADER_X;
    bool ded = false;
    bool gege = false;
    int8_t invaderYOffset = 0;
    int8_t cannonPxPosY = 8;
    int8_t livingInvaderCount = 0;
    int8_t invaderSpriteIdx = 0;
    int8_t invaderSpriteHeight =
        calculate_invader_sprite_height(invaderSpriteIdx);
    int8_t invaderUpdateCycleThresh = 96;

    InvaderConfigFlags invaderConfigPerScreenChar[SCREEN_CH_HEIGHT]
                                                 [SCREEN_CH_WIDTH];

    CannonProjectileData cannonProjData = {.pxPos = {0, 0}, .isActive = false};

    for (int i = 0; i < SCREEN_CH_HEIGHT; i++) {
      for (int j = START_INVADER_X; j < SCREEN_CH_WIDTH; j++) {
        invaderConfigPerScreenChar[i][j] =
            INVADER_CONFIG_FLAG_BOT | INVADER_CONFIG_FLAG_TOP;
        livingInvaderCount += 2;
      }
    }

    update_sprites_in_dd(invaderConfigPerScreenChar, START_INVADER_X);
    update_sprites_in_cg(invaderSpriteIdx, invaderYOffset, invaderSpriteHeight);

    while (true) {
      // Update invader sprites
      if (invaderUpdateCycles > invaderUpdateCycleThresh) {
        invaderSpriteIdx = 1 - invaderSpriteIdx;
        invaderSpriteHeight = calculate_invader_sprite_height(invaderSpriteIdx);

        if (currentInvaderDir == INVADER_DIRECTION_DOWN) {
          invaderYOffset = 1;
        } else if (currentInvaderDir == INVADER_DIRECTION_UP) {
          invaderYOffset = 0;
        } else if (currentInvaderDir == INVADER_DIRECTION_SIDE_FROM_DOWN ||
                   currentInvaderDir == INVADER_DIRECTION_SIDE_FROM_UP) {
          shift_sprites_left_in_dd(invaderConfigPerScreenChar);
          --currentInvaderStartX;
          update_sprites_in_dd(invaderConfigPerScreenChar,
                               currentInvaderStartX);
          invaderUpdateCycleThresh -= invaderUpdateCycles * 0.1f;

          if (currentInvaderStartX == 0) {
            ded = true;
            break;
          }
        }

        update_sprites_in_cg(invaderSpriteIdx, invaderYOffset,
                             invaderSpriteHeight);
        invaderUpdateCycles = 0;
        currentInvaderDir = get_next_invader_direction(currentInvaderDir);
      }

      // Update and redraw cannon

      int8_t const cannonRowPosOffset = is_button_down(BUTTON1)   ? -1
                                        : is_button_down(BUTTON5) ? 1
                                                                  : 0;
      cannonPxPosY =
          clamp(cannonPxPosY + cannonRowPosOffset, 0, SCREEN_PX_HEIGHT - 1);

      lcd_send_command(CG_RAM_ADDR + CANNON_CG_IDX * CHAR_HEIGHT);
      for (int i = 0; i < CHAR_HEIGHT; i++) {
        lcd_send_data(i == cannonPxPosY % CHAR_HEIGHT ? 1 : 0);
      }

      bool const cannonInBotHalf = cannonPxPosY >= SCREEN_PX_HEIGHT / 2;
      lcd_send_command(DD_RAM_ADDR);
      lcd_send_data(cannonInBotHalf ? ' ' : CANNON_CG_IDX);
      lcd_send_command(DD_RAM_ADDR2);
      lcd_send_data(cannonInBotHalf ? CANNON_CG_IDX : ' ');

      // Calculate collision, then update and redraw cannon projectile

      if (!is_cannon_proj_active(&cannonProjData)) {
        if (is_button_down(BUTTON3)) {
          set_cannon_projectile_active(&cannonProjData, cannonPxPosY);
        }
      } else {
        cannonProjData.pxPos.x += 1;
        Point const projCharPos = {cannonProjData.pxPos.x / CHAR_WIDTH,
                                   cannonProjData.pxPos.y / CHAR_HEIGHT};
        Point const projLocalPxPos = {cannonProjData.pxPos.x % CHAR_WIDTH,
                                      cannonProjData.pxPos.y % CHAR_HEIGHT};
        uint8_t const projRowAddr =
            get_dd_row_addr_from_px_y(cannonProjData.pxPos.y);

        if (is_cannon_projectile_out(&cannonProjData)) {
          set_cannon_projectile_inactive(&cannonProjData);
          lcd_send_command(projRowAddr + SCREEN_CH_WIDTH - 1);
          lcd_send_data(get_dd_value_from_config(
              invaderConfigPerScreenChar[projCharPos.y][SCREEN_CH_WIDTH - 1]));
        } else {
          InvaderConfigFlags const projCharInvaderConfigFlags =
              invaderConfigPerScreenChar[projCharPos.y][projCharPos.x];

          bool collision = false;

          if (projCharInvaderConfigFlags != INVADER_CONFIG_FLAG_NONE) {
            bool const hasTopInvader =
                (projCharInvaderConfigFlags & INVADER_CONFIG_FLAG_TOP) != 0;
            bool const hasBotInvader =
                (projCharInvaderConfigFlags & INVADER_CONFIG_FLAG_BOT) != 0;
            bool const collisionTop =
                projLocalPxPos.y < CHAR_HEIGHT / 2 && hasTopInvader;
            bool const collisionBot =
                projLocalPxPos.y >= CHAR_HEIGHT / 2 && hasBotInvader;
            collision = collisionTop || collisionBot;

            if (collision) {
              invaderConfigPerScreenChar[projCharPos.y][projCharPos.x] =
                  hasTopInvader && hasBotInvader
                      ? (collisionTop ? INVADER_CONFIG_FLAG_BOT
                                      : INVADER_CONFIG_FLAG_TOP)
                      : INVADER_CONFIG_FLAG_NONE;
              update_sprites_in_dd(invaderConfigPerScreenChar,
                                   currentInvaderStartX);
              set_cannon_projectile_inactive(&cannonProjData);
              currentInvaderStartX =
                  recalculate_invader_start_x(invaderConfigPerScreenChar);
              --livingInvaderCount;

              if (livingInvaderCount == 0) {
                gege = true;
                break;
              }

              // Clear character under the despawned projectile

              if (projCharPos.x > 1) {
                lcd_send_command(projRowAddr + projCharPos.x - 1);
                lcd_send_data(get_dd_value_from_config(
                    invaderConfigPerScreenChar[projCharPos.y]
                                              [projCharPos.x - 1]));
              }
            }
          }

          if (!collision) {
            // Move cannon projectile forward inside a character

            uint8_t const projPxRow = 1 << (CHAR_WIDTH - projLocalPxPos.x - 1);

            if (projCharInvaderConfigFlags != INVADER_CONFIG_FLAG_NONE) {
              lcd_send_command(projRowAddr + projCharPos.x);
              lcd_send_data(PROJ_INVADER_COMB_CG_IDX);

              uint8_t charRows[CHAR_HEIGHT] = {0};
              get_sprite_cg_char_from_config(
                  charRows, projCharInvaderConfigFlags, invaderSpriteIdx,
                  invaderSpriteHeight, invaderYOffset);
              charRows[projLocalPxPos.y] = projPxRow;

              lcd_send_command(CG_RAM_ADDR +
                               PROJ_INVADER_COMB_CG_IDX * CHAR_HEIGHT);
              for (int i = 0; i < CHAR_HEIGHT; i++) {
                lcd_send_data(charRows[i]);
              }
            } else {
              lcd_send_command(projRowAddr + projCharPos.x);
              lcd_send_data(CANNON_PROJ_CG_IDX);

              lcd_send_command(CG_RAM_ADDR + CANNON_PROJ_CG_IDX * CHAR_HEIGHT);
              for (int i = 0; i < CHAR_HEIGHT; i++) {
                lcd_send_data(i == projLocalPxPos.y ? projPxRow : 0);
              }
            }

            // Restore character behind projectile
            if (projLocalPxPos.x == 0) {
              int8_t const prevProjCharX = max(projCharPos.x - 1, 1);
              InvaderConfigFlags const configFlagsPrevX =
                  invaderConfigPerScreenChar[projCharPos.y][prevProjCharX];
              lcd_send_command(projRowAddr + prevProjCharX);
              lcd_send_data(get_dd_value_from_config(configFlagsPrevX));
            } else {
              lcd_send_data(' ');
            }
          }
        }
      }

      ++invaderUpdateCycles;
    }

    if (gege || ded) {
      lcd_send_line1(gege ? "    You won    " : "    You died    ");
      lcd_send_line2("               ");

      for (volatile unsigned int i = 0; i < 75; i++) {
        for (volatile unsigned int j = 0; j < UINT_MAX; j++) {
        }
      }

      lcd_send_line2(" Press a button ");

      while (true) {
        if (is_button_down(BUTTON1) || is_button_down(BUTTON2) ||
            is_button_down(BUTTON3) || is_button_down(BUTTON4) ||
            is_button_down(BUTTON5)) {
          lcd_send_line1(EMPTY_LINE);
          lcd_send_line2(EMPTY_LINE);
          break;
        }
      }
    }
  }
}
