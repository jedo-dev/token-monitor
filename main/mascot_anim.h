/* Сгенерировано tools/export_mascot.py — не править руками. */
#pragma once
#include <stdint.h>

#define MASCOT_SIZE   128
#define MASCOT_FRAMES 27

typedef struct { uint8_t frame; uint16_t ms; } mascot_step_t;

static const mascot_step_t MASCOT_IDLE[] = {{0, 450}, {1, 450}, {0, 450}, {1, 450}, {2, 140}, {0, 450}, {1, 450}, {0, 300}, {3, 900}, {0, 250}, {4, 900}, {0, 450}, {1, 450}, {0, 450}, {1, 200}, {5, 180}, {0, 200}, {5, 180}, {0, 450}, {6, 250}, {7, 250}, {6, 250}, {7, 250}, {0, 450}, {2, 140}, {0, 450}, {1, 450}};
static const mascot_step_t MASCOT_LAPTOP_IN[] = {{8, 110}, {9, 110}, {10, 110}};
static const mascot_step_t MASCOT_WORK[] = {{11, 150}, {12, 150}, {11, 150}, {12, 150}, {13, 150}, {12, 150}, {11, 150}, {14, 150}, {11, 150}, {12, 150}, {11, 150}, {12, 150}, {15, 700}, {16, 120}, {11, 150}, {12, 150}, {11, 150}, {14, 150}, {13, 150}, {12, 150}};
static const mascot_step_t MASCOT_LAPTOP_OUT[] = {{10, 110}, {9, 110}, {8, 110}};
static const mascot_step_t MASCOT_FALL_ASLEEP[] = {{17, 600}, {0, 300}, {17, 700}, {18, 600}, {19, 120}, {20, 120}, {24, 500}};
static const mascot_step_t MASCOT_SLEEP[] = {{21, 800}, {22, 800}, {23, 800}, {24, 800}};
static const mascot_step_t MASCOT_WAKE[] = {{25, 180}, {26, 350}, {0, 150}};
