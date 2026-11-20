#pragma once

#include <stddef.h>
#include <stdint.h>

struct HNode
{
    HNode *next = NULL;
    uint64_t hcode = 0;
};

struct Htable
{
    HNode **table = NULL; // array of slots
    size_t mask = 0; // power of 2 array size, 2^n - 1
    size_t size = 0; // number of keys
};

struct HMap
{
    Htable newer;
    Htable older;
    size_t migrate_pos = 0; // the position that we are copying from older to newer
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);

