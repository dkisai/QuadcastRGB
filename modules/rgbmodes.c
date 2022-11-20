/* quadcastrgb - change RGB mode for the microphone HyperX Quadcast S
 * File rgbmodes.c
 *
 * <----- License notice ----->
 * Copyright (C) 2022 Ors1mer
 *
 * You may contact the author by email:
 * ors1mer_dev [[at]] proton.me
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License ONLY.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see
 * <https://www.gnu.org/licenses/gpl-2.0.en.html>. For any questions
 * concerning the license, you can write to <licensing@fsf.org>.
 * Also, you may visit the Free Software Foundation at
 * 51 Franklin Street, Fifth Floor Boston, MA 02110 USA. 
 */
#include "rgbmodes.h"

static int count_data(struct colscheme *colsch);
static void fill_data(const struct colscheme *colsch, byte_t *da, int pckcnt);
static void equalize(int upper_size, int lower_size, datpack *da);
static void fillup_to(size_t copy_size, byte_t *curr, byte_t *finish);

/* Solid */
static void sequence_solid(const int *colors, int bright, byte_t *da);

/* Blink */
static int count_blink_data(struct colscheme *colsch);
static void sequence_blink_random(int bright, int speed, int dly_seg,
                                  byte_t *da);
static void sequence_blink(const struct colscheme *colsch, byte_t *da,
                           int pckcnt);
static void blink_segment_fill(int col, int col_seg, int dly_seg, int bright,
                               byte_t **da);
static void blink_color_fill(int color, int size, int bright, byte_t *da);
static int random_color();

/* Cycle */
static int count_cycle_data(struct colscheme *colsch);


/* Shared */
static void write_hexcolor(int color, int bright, byte_t *mem);

#ifdef DEBUG
static void print_datpack(datpack *da, int pck_cnt);
#endif

datpack *parse_colorscheme(struct colschemes *cs, int *pck_cnt)
{
    datpack *data_arr;
    int seq_upper, seq_lower;

    seq_upper = count_data(&cs->upper);
    seq_lower = count_data(&cs->lower);
    if(seq_upper < 1 || seq_lower < 1) {
        fprintf(stderr, NOSUPPORT_MSG);
        free(cs); exit(254);
    }

    *pck_cnt = seq_upper >= seq_lower ? seq_upper : seq_lower;
    data_arr = calloc(sizeof(datpack), *pck_cnt);

    fill_data(&cs->upper, *data_arr, *pck_cnt);
    fill_data(&cs->lower, *data_arr+BYTE_STEP, *pck_cnt);
    equalize(seq_upper, seq_lower, data_arr);

    #ifdef DEBUG
    print_datpack(data_arr, *pck_cnt);
    #endif

    return data_arr;
}

short count_color_commands(const datpack *data_arr, int pck_cnt, int colgroup)
{
    short cnt, step = 0;
    const byte_t *b;
    if(colgroup) /* case of lower color commands */
        step = BYTE_STEP;
    cnt = (pck_cnt-1)*8;
    for(b = data_arr[pck_cnt-1]; b < data_arr[pck_cnt-1]+DATA_PACKET_SIZE;
                                                         b += 2*BYTE_STEP) {
        if(*(b+step) != RGB_CODE)
            break;
        cnt++;
    }
    return cnt;
}

static int count_data(struct colscheme *colsch)
{
    if(strequ(colsch->mode, "solid")) {
        return 1;
    } else if(strequ(colsch->mode, "blink")) {
        return count_blink_data(colsch);
    } else if(strequ(colsch->mode, "cycle")) {
        return count_cycle_data(colsch);
    } else {
        return -1;
    }
}

static int count_blink_data(struct colscheme *colsch)
{
    int cnt = 0;
    int *col;

    if(colsch->colors[0] == nocolor) { /* case of random colors */
        srand(time(NULL)); /* random seed (must be done only once) */
        return MAX_PCT_COUNT;
    }

    for(col = colsch->colors; *col != nocolor; col++) {
        cnt += 101-colsch->spd + colsch->dly;
        if(cnt > MAX_COLPAIR_COUNT) {
            cnt -= 101-colsch->spd + colsch->dly;
            *col = nocolor; /* strip the sequence to avoid overflow */
            break;
        }
    }
    return DIV_CEIL(cnt, COLPAIR_PER_PCT);
}

static int count_cycle_data(struct colscheme *colsch)
{
    unsigned int color_cnt, size, tr_size;

    for(color_cnt = 0; colsch->colors[color_cnt] != nocolor; color_cnt++)
        {}

    tr_size = 100 - colsch->spd;
    /* The size of one transition: */
    size = MIN_CYCL_TR + (MAX_CYCL_TR - MIN_CYCL_TR)*tr_size/100;
    size *= color_cnt; /* the size of all colpairs */
    if(size > MAX_COLPAIR_COUNT) { /* case of overflow: fit in 720 colors */
        size = ( MIN_CYCL_TR + (MAX_COLPAIR_COUNT/color_cnt - MIN_CYCL_TR)*
                 tr_size/100 ) * color_cnt;
    }
    return DIV_CEIL(size, COLPAIR_PER_PCT);
}

static void fill_data(const struct colscheme *colsch, byte_t *da, int pckcnt)
{
    if(strequ(colsch->mode, "solid")) {
        sequence_solid(colsch->colors, colsch->br, da);
    } else if(strequ(colsch->mode, "blink")) {
        if(colsch->colors[0] == nocolor) {
            sequence_blink_random(colsch->br, colsch->spd, colsch->dly, da);
        }
        sequence_blink(colsch, da, pckcnt);
    }
}

static void equalize(int upper_size, int lower_size, datpack *da)
{
    enum { upper = 0, lower };
    byte_t *upper_end, *lower_end;
    upper_size = count_color_commands(da, upper_size, upper);
    lower_size = count_color_commands(da, lower_size, lower);
    upper_end = *da + 2*BYTE_STEP*(upper_size-1);
    lower_end = *da + 2*BYTE_STEP*(lower_size-1) + BYTE_STEP;

    if(upper_size < lower_size) {
        fillup_to(upper_size, upper_end+2*BYTE_STEP, lower_end-BYTE_STEP);
    } else if(lower_size < upper_size) {
        fillup_to(lower_size, lower_end+2*BYTE_STEP, upper_end+BYTE_STEP);
    } /* else equalizing isn't needed */
}

static void fillup_to(size_t copy_size, byte_t *curr, byte_t *finish)
{
    while(curr <= finish) {
        memcpy(curr, curr-(2*BYTE_STEP*copy_size), BYTE_STEP);
        curr += 2*BYTE_STEP;
    }
}


/* Mode-related functions */
static void sequence_solid(const int *colors, int bright, byte_t *da)
{
    *da = RGB_CODE; /* write code to the first byte */
    write_hexcolor(*colors, bright, da+1); /* write RGB */
}

static void sequence_blink_random(int bright, int speed, int delay,
                                  byte_t *da)
{
    int colpair = 0;
    int col_seg, dly_seg;

    col_seg = RAND_COL_SEG_MIN +
              (int)(speed * (RAND_COL_SEG_MAX-RAND_COL_SEG_MIN)) / MAX_SPD;
    dly_seg = RAND_DLY_SEG_MIN +
              (int)(delay * (RAND_DLY_SEG_MAX-RAND_DLY_SEG_MIN)) / MAX_DLY;
     
    while(colpair < MAX_COLPAIR_COUNT) {
        colpair += col_seg + dly_seg;
        if(colpair > MAX_COLPAIR_COUNT) /* strip color segment if overflow */
            col_seg -= colpair - MAX_COLPAIR_COUNT;
        blink_segment_fill(random_color(), col_seg, dly_seg, bright, &da);
    }
}

static void sequence_blink(const struct colscheme *colsch, byte_t *da,
                           int pckcnt)
{
    const int *col;
    int colpair = 0;
    int col_seg = 101 - colsch->spd;
    for(col = colsch->colors; *col != nocolor; col++) {
        blink_segment_fill(*col, col_seg, colsch->dly, colsch->br, &da);
        colpair += col_seg + colsch->dly;
    }
}

static void blink_segment_fill(int col, int col_seg, int dly_seg, int bright,
                               byte_t **da)
{
    blink_color_fill(col, col_seg, bright, *da);
    *da += 2*BYTE_STEP*col_seg;
    blink_color_fill(black, dly_seg, bright, *da);
    *da += 2*BYTE_STEP*dly_seg;
}

static void blink_color_fill(int color, int size, int bright, byte_t *da)
{
    int i = 0;
    for(; i < size; i++) {
        *da = RGB_CODE;
        write_hexcolor(color, bright, da+1);
        da += 2*BYTE_STEP;
    }
}

static int random_color()
{
    /* Generates a pseudorandom number from 0x1 to 0xffffff */
    return 1 + (int)(16777215.0*rand()/(RAND_MAX+1.0));
}

static void write_hexcolor(int color, int bright, byte_t *mem)
{
    int n;
    for(n = 16; n >= 0; n -= 8) {
        *mem = (byte_t)( (((color >> n) & 0xff)*bright) / 100 );
        mem++;
    }
}

#ifdef DEBUG
static void print_datpack(datpack *da, int pck_cnt)
{
    int i, j;
    printf(N_("Packets to be sent: %d\n"), pck_cnt);
    for(j = 0; j < pck_cnt; j++) {
        printf(N_("Packet %d:\n"), j+1);
        for(i = 0; i < DATA_PACKET_SIZE; i++) {
            printf("%02X ", (unsigned int)da[j][i]);
            if((i+1) % 4 == 0)
                printf("\t");
            if((i+1) % 8 == 0)
                puts("");
        }
        puts("");
    }
}
#endif
