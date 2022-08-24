#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <ctype.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>

FILE *debug_stream = NULL;
#define eprintf(args...) fprintf(debug_stream, args)

int wrap(int v, int m)
{
    while (v < 0)
        v += m;
    while (v >= m)
        v -= m;
    return v;
}
int clampl(int v, int m)
{
    return v < m ? m : v;
}
int clamph(int v, int m)
{
    return v > m ? m : v;
}

#define RC_SUCCESS 0
#define RC_WAS_A_MINE 1
#define RC_NEW_GAME 2
#define RC_EXIT 3
#define RC_OUT_OF_BOUNDS 4

#define MINE_STATE_NORMAL 0
#define MINE_STATE_MINED 1
#define MINE_STATE_FLAG 2
#define MINE_STATE_UNKNOWN 3

typedef struct mine
{
    unsigned char is_mine : 1;
    unsigned char state : 2;
    unsigned char nearby : 5;
} mine_t;

typedef struct minefield
{
    int width;
    int height;
    int mine_count;

    int selection_x;
    int selection_y;
    int unmined_free_space_count;
    int flag_count;
    int generated : 1;
    int gameover : 1;
    int gameover_win : 1;

    mine_t *mines;
} minefield_t;

mine_t *minefield_get_mine(minefield_t *mf, int x, int y)
{
    return mf->mines + (y * mf->width + x);
}

void minefield_draw_mine_internal(minefield_t *mf, int x, int y)
{
    mine_t *m = minefield_get_mine(mf, x, y);
    if (mf->selection_x == x && mf->selection_y == y)
        printf("\033[7m");

    if (m->state == MINE_STATE_MINED && m->is_mine)
        printf("\033[1;31mx \033[0m");
    else if (m->state == MINE_STATE_MINED && m->nearby > 0)
    {
        char *cc;
        if (m->nearby == 1)
            cc = "1;32";
        else if (m->nearby == 2)
            cc = "32";
        else if (m->nearby == 3)
            cc = "1;33";
        else if (m->nearby == 4)
            cc = "33";
        else if (m->nearby == 5)
            cc = "1;31";
        else if (m->nearby == 6)
            cc = "31";
        else if (m->nearby == 7)
            cc = "35";
        else if (m->nearby == 8)
            cc = "1;35";
        else
            cc = "1;37";

        printf("\033[%sm%d \033[0m", cc, m->nearby);
    }
    else if (m->state == MINE_STATE_NORMAL)
        printf(". ");
    else if (m->state == MINE_STATE_FLAG)
        printf("\033[1;36mf \033[0m");
    else if (m->state == MINE_STATE_UNKNOWN)
        printf("\033[1;34m? \033[0m");
    else if (m->state == MINE_STATE_MINED && m->nearby == 0)
        printf("  ");
    else
    {
        printf("E ");
        if (mf->selection_x == x && mf->selection_y == y)
            printf("\033[0m");
        eprintf("Invalid state %d for mine (is_mine = %d) (nearby = %d) at %d %d\n",
                m->state, m->is_mine, m->nearby, x, y);
        return;
    }
    if (mf->selection_x == x && mf->selection_y == y)
        printf("\033[0m");
}

void minefield_draw_mine(minefield_t *mf, int x, int y)
{
    printf("\033[s");
    printf("\033[%d;%dH", y + 1, x * 2 + 1);
    minefield_draw_mine_internal(mf, x, y);
    printf("\033[u");
}

void minefield_generate(minefield_t *mf)
{
    if (mf->generated)
        return;
    mf->generated = 1;

    struct skip_section
    {
        int start;
        int length;
    } skips[3] = {0};

    int sl = 1;
    int sr = 2;
    if (mf->selection_x - 1 < 0)
        sl--;
    if (mf->selection_x + 1 >= mf->width)
        sr--;

    if (mf->selection_y - 1 >= 0)
    {
        skips[0].start = (mf->selection_y - 1) * mf->width + mf->selection_x - sl;
        skips[0].length = sl + sr;
    }
    skips[1].start = mf->selection_y * mf->width + mf->selection_x - sl;
    skips[1].length = sl + sr;
    if (mf->selection_y + 1 < mf->height)
    {
        skips[2].start = (mf->selection_y + 1) * mf->width + mf->selection_x - sl;
        skips[2].length = sl + sr;
    }

    for (int m = 0; m < mf->mine_count; m++)
    {
        int mp = rand() % (mf->width * mf->height - m - skips[0].length - skips[1].length - skips[2].length);

        for (int x = 0; x <= mp; x++)
        {
            for (int s = 0; s < 3; s++)
                if (x == skips[s].start)
                {
                    mp += skips[s].length;
                    x += skips[s].length;
                }
            mine_t *m = mf->mines + mp;
            if (m->is_mine)
            {
                mp++;
                x++;
            }
        }
        eprintf("Placing mine at %d\n", mp);
        mf->mines[mp].is_mine = 1;
    }

    for (int x = 0; x < mf->width; x++)
    {
        for (int y = 0; y < mf->height; y++)
        {
            mine_t *m = minefield_get_mine(mf, x, y);
            for (int _x = clampl(x - 1, 0); _x <= clamph(x + 1, mf->width - 1); _x++)
            {
                for (int _y = clampl(y - 1, 0); _y <= clamph(y + 1, mf->height - 1); _y++)
                {
                    m->nearby += minefield_get_mine(mf, _x, _y)->is_mine;
                }
            }
        }
    }

    mf->unmined_free_space_count = mf->width * mf->height - mf->mine_count;
}

int minefield_mine(minefield_t *mf, int x, int y, int a)
{
    if (mf->gameover)
        return RC_SUCCESS;
    if (!mf->generated)
        minefield_generate(mf);
    mine_t *m = minefield_get_mine(mf, x, y);
    eprintf("minefield_mine(%d, %d)\n", x, y);
    if (m->state == MINE_STATE_NORMAL)
    {
        m->state = MINE_STATE_MINED;
        minefield_draw_mine(mf, x, y);
        if (m->is_mine)
            return RC_WAS_A_MINE;
        mf->unmined_free_space_count--;
        return minefield_mine(mf, x, y, 1);
    }
    else if (m->state == MINE_STATE_MINED)
    {
        int dm = 0;
        if (!m->nearby)
            dm = 1;
        if (!dm && !a)
        {
            for (int _x = clampl(x - 1, 0); _x <= clamph(x + 1, mf->width - 1); _x++)
            {
                for (int _y = clampl(y - 1, 0); _y <= clamph(y + 1, mf->height - 1); _y++)
                {
                    if (minefield_get_mine(mf, _x, _y)->state == MINE_STATE_FLAG)
                        dm++;
                }
            }

            if (dm != m->nearby)
                dm = 0;
        }
        if (dm)
        {
            int rc = RC_SUCCESS;
            for (int _x = clampl(x - 1, 0); _x <= clamph(x + 1, mf->width - 1); _x++)
            {
                for (int _y = clampl(y - 1, 0); _y <= clamph(y + 1, mf->height - 1); _y++)
                {
                    if (minefield_get_mine(mf, _x, _y)->state == MINE_STATE_NORMAL &&
                        (rc = minefield_mine(mf, _x, _y, a)) != RC_SUCCESS)
                        return rc;
                }
            }
        }
    }
    return RC_SUCCESS;
}

int minefield_flag(minefield_t *mf, int x, int y)
{
    if (mf->gameover)
        return RC_SUCCESS;
    if (!mf->generated)
        return RC_SUCCESS;
    mine_t *m = minefield_get_mine(mf, x, y);
    eprintf("minefield_flag(%d, %d)\n", x, y);
    if (m->state == MINE_STATE_MINED)
        return RC_SUCCESS;
    eprintf("Rotating flag %d -> ", m->state);
    if (m->state == MINE_STATE_NORMAL)
    {
        m->state = MINE_STATE_FLAG;
        mf->flag_count++;
    }
    else if (m->state == MINE_STATE_FLAG)
    {
        m->state = MINE_STATE_UNKNOWN;
        mf->flag_count--;
    }
    else
    {
        m->state = MINE_STATE_NORMAL;
    }
    eprintf("%d\n", m->state);
    minefield_draw_mine(mf, x, y);
}

void minefield_draw_clear(minefield_t *mf)
{
    eprintf("minefield_draw_clear()\n");
    printf("\033[s");
    printf("\033[H");
    fflush(stdout);
    for (int y = 0; y < mf->height; y++)
    {
        for (int x = 0; x < mf->width; x++)
        {
            minefield_draw_mine_internal(mf, x, y);
        }
        printf("\n");
    }
    printf("\033[u");
    fflush(stdout);
}

int game_loop(minefield_t *mf)
{
    static char c = 0;
#define read_char()          \
    if (read(0, &c, 1) != 1) \
    return RC_SUCCESS
    printf("\033[%d;%dH", mf->height + 1, 1);
    printf("X: % 3d Y: % 3d M: % 3d/% 3d", mf->selection_x, mf->selection_y,
           mf->mine_count - mf->flag_count, mf->mine_count);

    if (mf->gameover)
        if (mf->gameover_win)
            printf("%-10s", " You win!");
        else
            printf("%-10s", " You lost!");
    else
        printf("%-10s", "");
    printf("\n");

    fflush(stdout);
    fflush(debug_stream);

    read_char();

    printf("\033[J");

    if (c == 033)
    {
        read_char();
        if (c == '[')
        {
            read_char();
            if (c == 'A')
                c = 'w';
            else if (c == 'B')
                c = 's';
            else if (c == 'C')
                c = 'd';
            else if (c == 'D')
                c = 'a';
            else
            {
                eprintf("Unimplemented escape sequence: 0x1B ('<ESC>') 0x5B ('[') ");
                if (isprint(c))
                    eprintf("0x%02hhX ('%c')\n", c, c);
                else
                    eprintf("0x%02hhX\n", c);
            }
        }
        else
        {
            eprintf("Unimplemented escape sequence: 0x1B 0x%02hhX\n", c);
        }
    }

    // up up down down left right left right b a

    int old_x = mf->selection_x;
    int old_y = mf->selection_y;

    if (c == 'q')
        return RC_EXIT;
    else if (c == 'a')
    {
        mf->selection_x = wrap(mf->selection_x - 1, mf->width);
    }
    else if (c == 'd')
    {
        mf->selection_x = wrap(mf->selection_x + 1, mf->width);
    }
    else if (c == 'w')
    {
        mf->selection_y = wrap(mf->selection_y - 1, mf->height);
    }
    else if (c == 's')
    {
        mf->selection_y = wrap(mf->selection_y + 1, mf->height);
    }
    else if (c == 'f')
    {
        minefield_flag(mf, mf->selection_x, mf->selection_y);
    }
    else if (c == 'A')
    {
        mf->selection_x = wrap(mf->selection_x - 1, mf->width);
        mine_t *m = minefield_get_mine(mf, mf->selection_x, mf->selection_y);
        for (; m->state == MINE_STATE_MINED && m->nearby == 0;
             m = minefield_get_mine(mf, mf->selection_x = wrap(mf->selection_x - 1, mf->width), mf->selection_y))
            ;
    }
    else if (c == 'D')
    {
        mf->selection_x = wrap(mf->selection_x + 1, mf->width);
        mine_t *m = minefield_get_mine(mf, mf->selection_x, mf->selection_y);
        for (; m->state == MINE_STATE_MINED && m->nearby == 0;
             m = minefield_get_mine(mf, mf->selection_x = wrap(mf->selection_x + 1, mf->width), mf->selection_y))
            ;
    }
    else if (c == 'W')
    {
        mf->selection_y = wrap(mf->selection_y - 1, mf->height);
        mine_t *m = minefield_get_mine(mf, mf->selection_x, mf->selection_y);
        for (; m->state == MINE_STATE_MINED && m->nearby == 0;
             m = minefield_get_mine(mf, mf->selection_x, mf->selection_y = wrap(mf->selection_y - 1, mf->height)))
            ;
    }
    else if (c == 'S')
    {
        mf->selection_y = wrap(mf->selection_y + 1, mf->height);
        mine_t *m = minefield_get_mine(mf, mf->selection_x, mf->selection_y);
        for (; m->state == MINE_STATE_MINED && m->nearby == 0;
             m = minefield_get_mine(mf, mf->selection_x, mf->selection_y = wrap(mf->selection_y + 1, mf->height)))
            ;
    }
    else if (c == 'e')
    {
        if (minefield_mine(mf, mf->selection_x, mf->selection_y, 0))
        {
            for (int x = 0; x < mf->width; x++)
            {
                for (int y = 0; y < mf->height; y++)
                {
                    mine_t *m = minefield_get_mine(mf, x, y);
                    if (m->is_mine)
                        minefield_mine(mf, x, y, 1);
                }
            }
            mf->gameover = 1;
            mf->gameover_win = 0;
        }
        else
        {
            if (mf->unmined_free_space_count <= 0)
            {
                mf->gameover = 1;
                mf->gameover_win = 1;
            }
        }
    }
    else if (c == 'l')
    {
        for (int y = 0; y < mf->height; y++)
        {
            for (int x = 0; x < mf->width; x++)
            {
                mine_t *m = minefield_get_mine(mf, x, y);
                if (!m->is_mine)
                {
                    if (m->state == MINE_STATE_FLAG)
                        minefield_flag(mf, x, y);
                    if (m->state == MINE_STATE_UNKNOWN)
                        minefield_flag(mf, x, y);
                    minefield_mine(mf, x, y, 0);
                }
                else
                {
                    if (m->state == MINE_STATE_UNKNOWN)
                        minefield_flag(mf, x, y);
                    if (m->state == MINE_STATE_NORMAL)
                        minefield_flag(mf, x, y);
                }
            }
        }
        mf->gameover = 1;
        mf->gameover_win = 1;
    }
    else if (c == 'n')
    {
        return RC_NEW_GAME;
    }
    else if (c == 'c')
    {
        minefield_draw_clear(mf);
    }
    else
    {
        eprintf("Unimplemented character code: ");
        if (isprint(c))
            eprintf("0x%02hhX ('%c')\n", c, c);
        else
            eprintf("0x%02hhX\n", c);
    }

    if (old_x != mf->selection_x || old_y != mf->selection_y)
    {
        minefield_draw_mine(mf, old_x, old_y);
        minefield_draw_mine(mf, mf->selection_x, mf->selection_y);
    }

    return RC_SUCCESS;
}

int game_start(int w, int h, int m)
{
    eprintf("game_start(%d, %d, %d)\n", w, h, m);
    minefield_t *mf = malloc(sizeof(minefield_t));
    eprintf("malloc minefield -> 0x%p\n", mf);
    memset(mf, 0, sizeof(minefield_t));
    mf->width = w;
    mf->height = h;
    mf->mine_count = m;
    eprintf("mf->width = %d\n", mf->width);
    eprintf("mf->height = %d\n", mf->height);
    eprintf("sizeof(mine_t) = %d\n", sizeof(mine_t));
    mf->mines = malloc(mf->width * mf->height * sizeof(mine_t));
    eprintf("malloc mines (%d) -> 0x%p\n", mf->width * mf->height * sizeof(mine_t), mf->mines);
    memset(mf->mines, 0, mf->width * mf->height * sizeof(mine_t));

    printf("\033[H\033[J");

    minefield_draw_clear(mf);

    int e = RC_SUCCESS;
    while (e == RC_SUCCESS)
        e = game_loop(mf);

    free(mf->mines);
    free(mf);

    return e;
}

int color_test()
{
    eprintf("color_test_start()\n");
    minefield_t *mf = malloc(sizeof(minefield_t));
    eprintf("malloc minefield -> 0x%p\n");
    memset(mf, 0, sizeof(minefield_t));
    mf->width = 13;
    mf->height = 2;
    mf->mine_count = 4;
    mf->mines = malloc(mf->width * mf->height * sizeof(mine_t));
    eprintf("malloc mines (%d) -> 0x%p\n", mf->width * mf->height * sizeof(mine_t), mf->mines);

    mine_t *m = mf->mines;
    m->is_mine = 0;
    m->state = MINE_STATE_NORMAL;
    m->nearby = 0;
    m++;
    m->is_mine = 1;
    m->state = MINE_STATE_FLAG;
    m->nearby = 0;
    m++;
    m->is_mine = 0;
    m->state = MINE_STATE_UNKNOWN;
    m->nearby = 0;
    m++;
    m->is_mine = 1;
    m->state = MINE_STATE_MINED;
    m->nearby = 0;
    m++;
    for (int i = 0; i < 9; i++)
    {
        m->is_mine = 0;
        m->state = MINE_STATE_MINED;
        m->nearby = i;
        m++;
    }
    memcpy(mf->mines + mf->width, mf->mines, mf->width);

    printf("\033[H\033[J\033[3H");
    fflush(stdout);
    mf->selection_y = 1;
    mf->selection_x = 0;
    minefield_draw_clear(mf);
    for (int i = 0; i < mf->width; i++)
    {
        mf->selection_x = i;
        minefield_draw_mine(mf, i, 1);
    }
}

int main(int argc, char **argv)
{
    int w = 0;
    int h = 0;
    int m = 0;
    int f = 0;
    int ct = 0;
    char *df = NULL;

    int c;
    while ((c = getopt(argc, argv, "d:z:fc")) != -1)
    {
        switch (c)
        {
        case 'd':
            if (optarg[0] == 'e')
            {
                w = 10;
                h = 10;
                m = 10;
            }
            else if (optarg[0] == 'm' || optarg[0] == 'n')
            {
                w = 16;
                h = 16;
                m = 40;
            }
            else if (optarg[0] == 'h')
            {
                w = 30;
                h = 16;
                m = 99;
            }
            else
            {
                fprintf(stderr, "Invalid difficulty!\n");
                return 1;
            }
            break;
        case 'f':
            f = 1;
            break;
        case 'z':
            df = optarg;
            break;
        case 'c':
            ct = 1;
            break;
        default:
            printf("Invalid arg!\n");
            return 1;
        }
    }

    if (df == NULL)
    {
        debug_stream = fopen("/dev/null", "w");
    }
    else
    {
        debug_stream = fopen(df, "w");
    }

    if (ct == 1)
    {
        eprintf("Ignoring field size and mine count due to color test.\n");
        color_test();
        fclose(debug_stream);
        return 0;
    }

    if (f)
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
                return 1;
        w = ws.ws_col / 2;
        h = ws.ws_row - 2;
        if (m == 10)
            m = 0.1f * w * h;
        else if (m == 40)
            m = 0.156f * w * h;
        else if (m == 99)
            m = 0.206f * w * h;
        eprintf("w %d h %d m %d\n", w, h);
    }

    if (m == 0)
    {
        fprintf(stderr, "No difficulty set!\n");
        fprintf(stderr, "Usage: \"%s\" -d <e|n|h> [-f]\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    // Disable buffer
    struct termios old = {0};
    struct termios new;
    if (tcgetattr(0, &old) < 0)
        eprintf("Failed to get console attributes.\n");
    memcpy(&new, &old, sizeof(struct termios));
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &new) < 0)
        eprintf("Failed to set console attributes.\n");

    int e = RC_NEW_GAME;
    while (e == RC_NEW_GAME)
        e = game_start(w, h, m);

    // "Fix" terminal
    if (tcsetattr(0, TCSANOW, &old) < 0)
        eprintf("Failed to restore console attributes.\n");

    fclose(debug_stream);

    return 0;
}
