#ifndef POKEMMOPS5_CURSOR_ARROW_H
#define POKEMMOPS5_CURSOR_ARROW_H

/* From local ps5-proton/ps5-wow/ps5/title/cursor-arrow.h.
 * The source file contains no explicit license notice. */
#define POKEMMOPS5_CURSOR_WIDTH 12
#define POKEMMOPS5_CURSOR_HEIGHT 19

/* # is the opaque black outline, . the opaque white fill, and space transparent. */
static const char pokemmops5_cursor_arrow[POKEMMOPS5_CURSOR_HEIGHT][POKEMMOPS5_CURSOR_WIDTH + 1] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.........# ",
    "#..........#",
    "#.....######",
    "#..#..#     ",
    "#.# #..#    ",
    "##  #..#    ",
    "#    #..#   ",
    "     #..#   ",
    "      ##    ",
};

#endif
