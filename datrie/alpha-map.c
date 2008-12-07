/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * alpha-map.c - map between character codes and trie alphabet
 * Created: 2006-08-19
 * Author:  Theppitak Karoonboonyanan <thep@linux.thai.net>
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "alpha-map.h"
#include "fileutils.h"

/*------------------------*
 *   INTERNAL FUNCTIONS   *
 *------------------------*/
static int alpha_char_strlen (const AlphaChar *str);

/*----------------------------------------*
 *   INTERNAL FUNCTIONS IMPLEMENTATIONS   *
 *----------------------------------------*/

static int
alpha_char_strlen (const AlphaChar *str)
{
    const AlphaChar *p;

    for (p = str; *p; p++)
        ;
    return p - str;
}

/*-----------------------------------*
 *    PRIVATE METHODS DECLARATIONS   *
 *-----------------------------------*/
static AlphaMap * alpha_map_new ();
static int        alpha_map_add_range (AlphaMap *alpha_map,
                                       AlphaChar begin,
                                       AlphaChar end);

/*------------------------------*
 *    PRIVATE DATA DEFINITONS   *
 *------------------------------*/

typedef struct _AlphaRange {
    struct _AlphaRange *next;

    AlphaChar           begin;
    AlphaChar           end;
} AlphaRange;

struct _AlphaMap {
    AlphaRange     *first_range;
    AlphaRange     *last_range;
};

/*-----------------------------------*
 *    PRIVATE METHODS DECLARATIONS   *
 *-----------------------------------*/
static int  alpha_map_get_total_ranges (AlphaMap *alpha_map);

/*-----------------------------*
 *    METHODS IMPLEMENTAIONS   *
 *-----------------------------*/

#define ALPHAMAP_SIGNATURE  0xD9FCD9FC

/* AlphaMap Header:
 * - INT32: signature
 * - INT32: total ranges
 *
 * Ranges:
 * - INT32: range begin
 * - INT32: range end
 */

AlphaMap *
alpha_map_open (const char *path, const char *name, const char *ext)
{
    FILE       *file;
    char        line[256];
    AlphaMap   *alpha_map;

    file = file_open (path, name, ext, TRIE_IO_READ);
    if (!file)
        return NULL;

    /* prepare data */
    alpha_map = alpha_map_new ();
    if (!alpha_map)
        goto exit1;

    /* read character ranges */
    while (fgets (line, sizeof line, file)) {
        int         b, e;

        /* read the range
         * format: [b,e]
         * where: b = begin char, e = end char; both in hex values
         */ 
        if (sscanf (line, " [ %x , %x ] ", &b, &e) != 2)
            continue;
        if (b > e) {
            fprintf (stderr, "Range begin (%x) > range end (%x)\n", b, e);
            continue;
        }
        alpha_map_add_range (alpha_map, b, e);
    }

    fclose (file);
    return alpha_map;

exit1:
    fclose (file);
    return NULL;
}

static AlphaMap *
alpha_map_new ()
{
    AlphaMap   *alpha_map;

    alpha_map = (AlphaMap *) malloc (sizeof (AlphaMap));
    if (!alpha_map)
        return NULL;

    alpha_map->first_range = alpha_map->last_range = NULL;

    return alpha_map;
}

void
alpha_map_free (AlphaMap *alpha_map)
{
    AlphaRange *p, *q;

    p = alpha_map->first_range; 
    while (p) {
        q = p->next;
        free (p);
        p = q;
    }

    free (alpha_map);
}

AlphaMap *
alpha_map_read_bin (FILE *file)
{
    long        save_pos;
    uint32      sig;
    int32       total, i;
    AlphaMap   *alpha_map;

    /* check signature */
    save_pos = ftell (file);
    if (!file_read_int32 (file, (int32 *) &sig) || ALPHAMAP_SIGNATURE != sig) {
        fseek (file, save_pos, SEEK_SET);
        return NULL;
    }

    alpha_map = alpha_map_new ();
    if (!alpha_map)
        return NULL;

    /* read number of ranges */
    file_read_int32 (file, &total);

    /* read character ranges */
    for (i = 0; i < total; i++) {
        int32   b, e;

        file_read_int32 (file, &b);
        file_read_int32 (file, &e);
        alpha_map_add_range (alpha_map, b, e);
    }

    return alpha_map;
}

static int
alpha_map_get_total_ranges (AlphaMap *alpha_map)
{
    int         n;
    AlphaRange *range;

    for (n = 0, range = alpha_map->first_range; range; range = range->next) {
        ++n;
    }

    return n;
}

int
alpha_map_write_bin (AlphaMap *alpha_map, FILE *file)
{
    AlphaRange *range;

    if (!file_write_int32 (file, ALPHAMAP_SIGNATURE) ||
        !file_write_int32 (file, alpha_map_get_total_ranges (alpha_map)))
    {
        return -1;
    }

    for (range = alpha_map->first_range; range; range = range->next) {
        if (!file_write_int32 (file, range->begin) ||
            !file_write_int32 (file, range->end))
        {
            return -1;
        }
    }

    return 0;
}

static int
alpha_map_add_range (AlphaMap *alpha_map, AlphaChar begin, AlphaChar end)
{
    AlphaRange *range;

    if (begin > end)
        return -1;

    range = (AlphaRange *) malloc (sizeof (AlphaRange));
    if (!range)
        return -1;

    range->begin = begin;
    range->end   = end;

    /* append it to list of ranges */
    range->next = NULL;
    if (alpha_map->last_range) {
        alpha_map->last_range->next = range;
    } else {
        alpha_map->first_range = range;
    }
    alpha_map->last_range = range;

    return 0;
}

TrieChar
alpha_map_char_to_trie (const AlphaMap *alpha_map, AlphaChar ac)
{
    TrieChar    alpha_begin;
    AlphaRange *range;

    if (0 == ac)
        return 0;

    alpha_begin = 1;
    for (range = alpha_map->first_range;
         range && (ac < range->begin || range->end < ac);
         range = range->next)
    {
        alpha_begin += range->end - range->begin + 1;
    }
    if (range)
        return alpha_begin + (ac - range->begin);

    return TRIE_CHAR_MAX;
}

AlphaChar
alpha_map_trie_to_char (const AlphaMap *alpha_map, TrieChar tc)
{
    TrieChar    alpha_begin;
    AlphaRange *range;

    if (0 == tc)
        return 0;

    alpha_begin = 1;
    for (range = alpha_map->first_range;
         range && alpha_begin + (range->end - range->begin) < tc;
         range = range->next)
    {
        alpha_begin += range->end - range->begin + 1;
    }
    if (range)
        return range->begin + (tc - alpha_begin);

    return ALPHA_CHAR_ERROR;
}

TrieChar *
alpha_map_char_to_trie_str (const AlphaMap *alpha_map, const AlphaChar *str)
{
    TrieChar   *alphabet_str, *p;

    alphabet_str = (TrieChar *) malloc (alpha_char_strlen ((const char *)str)
                                        + 1);
    for (p = alphabet_str; *str; p++, str++) {
        *p = alpha_map_char_to_trie (alpha_map, *str);
    }
    *p = 0;

    return alphabet_str;
}

AlphaChar *
alpha_map_trie_to_char_str (const AlphaMap *alpha_map, const TrieChar *str)
{
    AlphaChar  *alpha_str, *p;

    alpha_str = (AlphaChar *) malloc ((strlen ((const char *)str) + 1)
                                      * sizeof (AlphaChar));
    for (p = alpha_str; *str; p++, str++) {
        *p = (AlphaChar) alpha_map_trie_to_char (alpha_map, *str);
    }
    *p = 0;

    return alpha_str;
}

/*
vi:ts=4:ai:expandtab
*/
