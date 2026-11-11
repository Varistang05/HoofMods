#pragma once

enum {
    PAD_UP = 0x0001, PAD_DOWN = 0x0002, PAD_LEFT = 0x0004, PAD_RIGHT = 0x0008,
    PAD_START = 0x0010, PAD_BACK = 0x0020, PAD_LB = 0x0100, PAD_RB = 0x0200,
    PAD_A = 0x1000, PAD_B = 0x2000, PAD_X = 0x4000, PAD_Y = 0x8000,
};

typedef struct {
    unsigned short buttons;
    float lx, ly, rx, ry, lt, rt;
} PadState;

/* first controller thats plugged in, 0 if none */
int pad_read(PadState *out);
