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


