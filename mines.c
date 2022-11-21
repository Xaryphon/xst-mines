#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>

#define MINE_STATE_NORMAL  0
#define MINE_STATE_MINED   1
#define MINE_STATE_FLAGGED 2
#define MINE_STATE_UNKNOWN 3

typedef enum minefield_status {
    MINEFIELD_STATUS_EMPTY     = 0,
    MINEFIELD_STATUS_GENERATED = 1,
    MINEFIELD_STATUS_VICTORY   = 2,
    MINEFIELD_STATUS_LOST      = 3,
} minefield_status_t;
#define MINEFIELD_STATUS_IS_GAME_OVER(status) (((status) | 1) == MINEFIELD_STATUS_LOST)

typedef struct mine {
    uint8_t is_mine : 1;
    uint8_t state   : 2;
    uint8_t _unused : 1;
    uint8_t nearby  : 4;
} mine_t;

typedef struct minefield {
    unsigned width;
    unsigned height;
    unsigned mine_count;

    minefield_status_t status;
    unsigned flag_count;
    unsigned unmined_count;

    unsigned changed_x0;
    unsigned changed_y0;
    unsigned changed_x1;
    unsigned changed_y1;

    mine_t data[/* width * height */];
} minefield_t;

void minefield_reset(minefield_t *field)
{
    field->status = MINEFIELD_STATUS_EMPTY;
    field->flag_count = 0;
    field->unmined_count = field->width * field->height - field->mine_count;
    field->changed_x0 = 0;
    field->changed_y0 = 0;
    field->changed_x1 = field->width;
    field->changed_y1 = field->height;
}

minefield_t *minefield_recreate(minefield_t *field, unsigned width, unsigned height, unsigned mine_count)
{
    minefield_t *new_field = realloc(field, sizeof(minefield_t) + width * height * sizeof(mine_t));
    if (new_field == NULL)
    {
        free(field);
        return NULL;
    }
    field = new_field;

    field->width = width;
    field->height = height;
    field->mine_count = mine_count;

    minefield_reset(field);

    return field;
}

minefield_t *minefield_create(unsigned width, unsigned height, unsigned mine_count)
{
    return minefield_recreate(NULL, width, height, mine_count);
}

void minefield_destroy(minefield_t *field)
{
    free(field);
}

void minefield_generate(minefield_t *field, unsigned x, unsigned y)
{
    assert(field->status == MINEFIELD_STATUS_EMPTY);
    assert(x < field->width && y < field->height);
    assert(field->mine_count <= field->width * field->height - 3*3);

    field->status = MINEFIELD_STATUS_GENERATED;

    memset(field->data, 0, field->width * field->height * sizeof(mine_t));

    unsigned safe_x0 = x == 0 ? 0 : x - 1;
    unsigned safe_y0 = y == 0 ? 0 : y - 1;
    unsigned safe_x1 = x == field->width - 1  ? field->width - 1  : x + 1;
    unsigned safe_y1 = y == field->height - 1 ? field->height - 1 : y + 1;

    unsigned safe_area = (safe_x1 - safe_x0 + 1) * (safe_y1 - safe_y0 + 1);

    for (unsigned i = 0; i < field->mine_count; i++) {
        unsigned r = rand();
        unsigned pos = r % (field->width * field->height - safe_area - i);
        for (unsigned j = 0; j <= pos; j++) {
            unsigned x = j % field->width;
            unsigned y = j / field->width;
            if ((safe_x0 <= x && x <= safe_x1 && safe_y0 <= y && y <= safe_y1) || field->data[j].is_mine)
                pos++;
        }

        unsigned pos_x = pos % field->width;
        unsigned pos_y = pos / field->width;
        field->data[pos].is_mine = 1;

        (void)pos_x; (void)pos_y;

        unsigned x0 = pos_x == 0 ? 0 : pos_x - 1;
        unsigned y0 = pos_y == 0 ? 0 : pos_y - 1;
        unsigned x1 = pos_x == field->width  - 1 ? field->width  - 1 : pos_x + 1;
        unsigned y1 = pos_y == field->height - 1 ? field->height - 1 : pos_y + 1;
        for (unsigned y = y0; y <= y1; y++) {
            for (unsigned x = x0; x <= x1; x++) {
                field->data[y * field->width + x].nearby++;
            }
        }
    }
}

mine_t *minefield_get_mine_ptr(minefield_t *field, unsigned x, unsigned y)
{
    assert(x < field->width && y < field->height);
    return &field->data[y * field->width + x];
}

mine_t minefield_get_mine(minefield_t *field, unsigned x, unsigned y)
{
    return *minefield_get_mine_ptr(field, x, y);
}

void _minefield_invalidate_at(minefield_t *field, unsigned x, unsigned y)
{
    if (field->changed_x0 > x) field->changed_x0 = x;
    if (field->changed_y0 > y) field->changed_y0 = y;
    if (field->changed_x1 <= x) field->changed_x1 = x + 1;
    if (field->changed_y1 <= y) field->changed_y1 = y + 1;
}

bool _minefield_mine_around(minefield_t *field, unsigned x, unsigned y);

bool _minefield_mine(minefield_t *field, unsigned x, unsigned y, bool is_user_input)
{
    // invalidate the area even if no change happened just because
    _minefield_invalidate_at(field, x, y);

    mine_t *m = minefield_get_mine_ptr(field, x, y);
    switch (m->state) {
    case MINE_STATE_NORMAL:
        m->state = MINE_STATE_MINED;
        if (m->is_mine) {
            field->status = MINEFIELD_STATUS_LOST;
            return false;
        }
        field->unmined_count--;
        if (field->unmined_count == 0) {
            field->status = MINEFIELD_STATUS_VICTORY;
        }
        if (m->nearby == 0) {
            return _minefield_mine_around(field, x, y);
        }
    case MINE_STATE_FLAGGED:
    case MINE_STATE_UNKNOWN:
        break;
    case MINE_STATE_MINED:
        if (is_user_input) {
            unsigned nearby_flags = 0;

            unsigned x0 = x == 0 ? 0 : x - 1;
            unsigned y0 = y == 0 ? 0 : y - 1;
            unsigned x1 = x == field->width - 1  ? field->width - 1  : x + 1;
            unsigned y1 = y == field->height - 1 ? field->height - 1 : y + 1;
            for (unsigned y = y0; y <= y1; y++) {
                for (unsigned x = x0; x <= x1; x++) {
                    nearby_flags += minefield_get_mine(field, x, y).state == MINE_STATE_FLAGGED;
                }
            }

            // NOTE: This expects m->state != FLAGGED
            if (nearby_flags == m->nearby)
                return _minefield_mine_around(field, x, y);
        }
        break;
    }
    return true;
}

bool _minefield_mine_around(minefield_t *field, unsigned x, unsigned y)
{
    unsigned x0 = x == 0 ? 0 : x - 1;
    unsigned y0 = y == 0 ? 0 : y - 1;
    unsigned x1 = x == field->width - 1  ? field->width - 1  : x + 1;
    unsigned y1 = y == field->height - 1 ? field->height - 1 : y + 1;
    for (unsigned y = y0; y <= y1; y++) {
        for (unsigned x = x0; x <= x1; x++) {
            if (!_minefield_mine(field, x, y, false))
                return false;
        }
    }
    return true;
}

void minefield_mine(minefield_t *field, unsigned x, unsigned y)
{
    assert(x < field->width && y < field->height);
    if (MINEFIELD_STATUS_IS_GAME_OVER(field->status))
        return;
    if (field->status == MINEFIELD_STATUS_EMPTY)
        minefield_generate(field, x, y);

    _minefield_mine(field, x, y, true);
}

void minefield_flag(minefield_t *field, unsigned x, unsigned y)
{
    assert(x < field->width && y < field->height);
    if (field->status != MINEFIELD_STATUS_GENERATED)
        return;

    // invalidate the area even if no change happened just because
    _minefield_invalidate_at(field, x, y);

    mine_t *m = &field->data[y * field->width + x];
    switch (m->state) {
    case MINE_STATE_NORMAL:
        m->state = MINE_STATE_FLAGGED;
        field->flag_count++;
        break;
    case MINE_STATE_FLAGGED:
        m->state = MINE_STATE_UNKNOWN;
        field->flag_count--;
        break;
    case MINE_STATE_UNKNOWN:
        m->state = MINE_STATE_NORMAL;
        break;
    case MINE_STATE_MINED:
        break;
    }
}

bool terminal_init(struct termios *old)
{
    struct termios new;
    if (tcgetattr(STDIN_FILENO, &new) < 0)
        return false;
    *old = new;
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0)
        return false;

    return true;
}

bool terminal_restore(struct termios *old)
{
    return tcsetattr(STDIN_FILENO, TCSANOW, old) >= 0;
}

bool terminal_try_restore(struct termios *old)
{
    if (!terminal_restore(old)) {
        printf("Failed to restore terminal attributes!\n");
        return false;
    }
    return true;
}

void print_status_line(minefield_t *field, unsigned x, unsigned y)
{
    printf("\033[%dH", field->height + 1); // move cursor to status line
    printf("\033[J"); // clear rest of the screen
    printf("\nP %u,%u  M %u/%u  C %u", x + 1, y + 1, field->mine_count - field->flag_count, field->mine_count, field->unmined_count);
    switch (field->status) {
    case MINEFIELD_STATUS_EMPTY:
        printf(" --empty--");
        break;
    case MINEFIELD_STATUS_GENERATED:
        printf(" ---generated---");
        break;
    case MINEFIELD_STATUS_VICTORY:
        printf(" You win!");
        break;
    case MINEFIELD_STATUS_LOST:
        printf(" You lose!");
        break;
    }
}

unsigned char minefield_get_mine_visual(minefield_t *field, unsigned x, unsigned y)
{
    mine_t mine = minefield_get_mine(field, x, y);

    if (field->status == MINEFIELD_STATUS_EMPTY || mine.state == MINE_STATE_NORMAL)
        return '.';
    else if (mine.state == MINE_STATE_FLAGGED)
        return 'f';
    else if (mine.state == MINE_STATE_UNKNOWN)
        return '?';
    else // MINE_STATE_MINED
    {
        if (mine.is_mine)
            return 'x';
        else
            return '0' + mine.nearby;
    }
}

// This thing takes 2 kB on 64-bit!
const char *const _mine_visual_colored_map[UCHAR_MAX] = {
    ['.'] = ".",
    ['f'] = "\033[1;36mf", // bold, cyan
    ['?'] = "\033[1;34m?", // bold, blue
    ['x'] = "\033[1;31mx", // bold, red
    ['0'] = " ",
    ['1'] = "\033[1;32m1", // bold, green
    ['2'] = "\033[32m2",   // green
    ['3'] = "\033[1;33m3", // bold, yellow
    ['4'] = "\033[33m4",   // yellow
    ['5'] = "\033[1;31m5", // bold, red
    ['6'] = "\033[31m6",   // red
    ['7'] = "\033[35m7",   // magenta
    ['8'] = "\033[1;35m8", // bold, magenta
};

void _minefield_draw_mine(minefield_t *field, unsigned cursor_x, unsigned cursor_y, unsigned x, unsigned y)
{
    if (x == cursor_x && y == cursor_y)
        printf("\033[7m"); // reverse video

    unsigned char c = minefield_get_mine_visual(field, x, y);
    assert(_mine_visual_colored_map[c]);
    printf("%s ", _mine_visual_colored_map[c]);

    printf("\033[m"); // reset attributes
}

void minefield_draw_region(minefield_t *field, unsigned cursor_x, unsigned cursor_y, unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
    for (unsigned y = y0; y < y1; y++) {
        printf("\033[%d;%dH", y + 1, x0 * 2 + 1);
        for (unsigned x = x0; x < x1; x++) {
            _minefield_draw_mine(field, cursor_x, cursor_y, x, y);
        }
    }
}

void minefield_draw_at(minefield_t *field, unsigned cursor_x, unsigned cursor_y, unsigned x, unsigned y)
{
    printf("\033[%d;%dH", y + 1, x * 2 + 1); // Set cursor position
    _minefield_draw_mine(field, cursor_x, cursor_y, x, y);
}

void minefield_draw_changed(minefield_t *field, unsigned cursor_x, unsigned cursor_y)
{
    minefield_draw_region(field, cursor_x, cursor_y, field->changed_x0, field->changed_x1, field->changed_y0, field->changed_y1);
}

void game_loop(minefield_t **field_ptr)
{
    minefield_t *field = *field_ptr;
    char c;
#define read_char() if (read(0, &c, 1) != 1) return

    unsigned old_cursor_x = 0;
    unsigned old_cursor_y = 0;
    unsigned cursor_x = 0;
    unsigned cursor_y = 0;

    for (;;) {
        print_status_line(*field_ptr, cursor_x, cursor_y);
        printf("\033[H");

        if (field->changed_x0 != UINT_MAX) {
            minefield_draw_changed(field, cursor_x, cursor_y);
            field->changed_x0 = UINT_MAX;
            field->changed_x1 = 0;
            field->changed_y0 = UINT_MAX;
            field->changed_y1 = 0;
        } else if (old_cursor_x != cursor_x || old_cursor_y != cursor_y) {
            minefield_draw_at(field, cursor_x, cursor_y, old_cursor_x, old_cursor_y);
            minefield_draw_at(field, cursor_x, cursor_y, cursor_x, cursor_y);
        }

        old_cursor_x = cursor_x;
        old_cursor_y = cursor_y;

        printf("\033[%d;%dH", cursor_y + 1, cursor_x * 2 + 1); // Set cursor position

        fflush(stdout);
        read_char();
        switch (c) {
        case 'w':
            if (cursor_y == 0)
                cursor_y = field->height;
            cursor_y--;
            break;

        case 's':
            cursor_y++;
            if (cursor_y == field->height)
                cursor_y = 0;
            break;

        case 'a':
            if (cursor_x == 0)
                cursor_x = field->width;
            cursor_x--;
            break;

        case 'd':
            cursor_x++;
            if (cursor_x == field->width)
                cursor_x = 0;
            break;

        case 'e':
            minefield_mine(field, cursor_x, cursor_y);
            break;

        case 'f':
            minefield_flag(field, cursor_x, cursor_y);
            break;

        case 'n':
            minefield_reset(field);
            break;

        case 'q':
            return;

        default: break;
        };
    }
}

int main(int argc, char **argv)
{
    srand(0);

    (void)argc;
    (void)argv;

    struct termios old;
    if (!terminal_init(&old)) {
        printf("Failed to initialize terminal!\n");
        return 1;
    }

    minefield_t *field = minefield_create(10, 10, 10);
    if (!field) {
        terminal_try_restore(&old);

        printf("Failed to create minefield!\n");
        return 1;
    }

    printf("\033[?1049h"); // enable alternative screen buffer
    printf("\033[3J\033[H\033[J"); // clear the screen, move cursor to top left, clear again
    printf("\033[?25l"); // hide cursor

    game_loop(&field);

    printf("\033[?25h"); // show cursor
    printf("\033[?1049l"); // disable alternative screen buffer

    minefield_destroy(field);
    terminal_try_restore(&old);
}
