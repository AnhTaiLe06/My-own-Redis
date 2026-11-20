#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

// n must be power of 2
static void h_init(Htable *htable, size_t n)
{
    assert(n > 0 && ((n - 1) & n) == 0); // n is the power of 2
    htable->table = (HNode **)calloc(n, sizeof(HNode *));
    htable->mask = n - 1;
    htable->size = 0;
}

static void h_insert(Htable *htable, HNode *node)
{
    size_t pos = node->hcode & htable->mask; // the position of slot in the hashtable the data should live in.
    node->next = htable->table[pos];
    htable->table[pos] = node; // insert the new node to the top of the array (or slot)
    htable->size++;
    return;
}

// Hashtable lookup
static HNode **h_lookup(Htable *htable, HNode *key, bool(*eq)(HNode *, HNode *))
{
    if(!htable->table)
    {
        return NULL;
    }

    size_t pos = key->hcode & htable->mask;
    HNode **from = &htable->table[pos]; // address of the parent pointer 
    for(HNode *node; (node = *from) != NULL; from = &(node->next))
    {
        if(node->hcode == key->hcode && eq(node, key))
        {
            return from; // for delete 
        }
    }
    return NULL;
}

// Hashtable deletion
static HNode *h_detach(Htable *htable, HNode **from)
{
    HNode *tmp = *from;
    *from = tmp->next;
    htable->size--;
    return tmp;
}

static void hm_trigger_rehashing(HMap *hmap)
{
    hmap->older = hmap->newer; // (newer, older) <-- (new_table, newer)
    h_init(&hmap->newer, (hmap->newer.mask + 1) << 1); // double size the new hash map
    hmap->migrate_pos = 0; // reset the copyed position to 0
}

const size_t k_max_rehashing_work = 128; // the maximum number of keys we migrate from old table to new table at a time

static void hm_help_rehashing(HMap *hmap)
{
    size_t cnt = 0;
    while(cnt < k_max_rehashing_work && hmap->older.size > 0)
    {
        HNode **from = &hmap->older.table[hmap->migrate_pos];
        if(!*from)
        {
            hmap->migrate_pos++;
            continue; // empty slot
        }

        h_insert(&hmap->newer, *from);
        h_detach(&hmap->older, from);
        cnt++;
    }

    // discard the old table when we're done migrating
    if(hmap->older.size == 0 && hmap->older.table)
    {
        free(hmap->older.table);
        hmap->older = Htable{};
    }
}

// both lookup and delete finding in both old and new table because data in either one.
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
    hm_help_rehashing(hmap); // migrate some keys from old to new table
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if(!from)
    {
        from = h_lookup(&hmap->older, key, eq);
    }
    return from ? *from : NULL;
}

HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
    hm_help_rehashing(hmap); // migrate some keys from old to new table
    HNode**from = h_lookup(&hmap->newer, key, eq);
    if(from)
    {
        return h_detach(&hmap->newer, from);
    }
    from = h_lookup(&hmap->older, key, eq);
    if(from)
    {
        return h_detach(&hmap->older, from);
    }
    return NULL;
}

const size_t k_max_load_factor = 8; // 2^3

void hm_insert(HMap *hmap, HNode *node)
{
    if(!hmap->newer.table)
    {
        h_init(&hmap->newer, 4); // initialize it if empty
    }

    h_insert(&hmap->newer, node);
    if(!hmap->older.table) // check whether we need to rehash
    {
        size_t shreshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if(hmap->newer.size >= shreshold)
        {
            hm_trigger_rehashing(hmap);
        }
    }

    hm_help_rehashing(hmap); // migrate some keys from old table to new table at a time
}

void hm_clear(HMap *hmap)
{
    free(hmap->newer.table);
    free(hmap->older.table);
    *hmap = HMap{};
}

size_t hm_size(HMap *hmap)
{
    return hmap->newer.size + hmap->older.size;
}
