#ifndef ENCHANT_DATA_H
#define ENCHANT_DATA_H

#include <stdint.h>

/* Ported 1:1 from https://github.com/iamcal/enchant-order data.js */

#define MAX_ENCHANT_ITEMS 24
#define MAX_ENCHANT_INCOMPATIBLE 8

typedef struct {
    const char *id;      /* namespace key, e.g. "protection" */
    const char *display; /* human-readable label */
    int level_max;
    int weight;
    const char *incompatible_ids[MAX_ENCHANT_INCOMPATIBLE]; /* NULL-terminated */
    const char *item_ids[MAX_ENCHANT_ITEMS];                /* NULL-terminated */

    /* resolved by enchant_data_init() */
    uint64_t incompatible_mask; /* bit per enchant index */
    uint32_t item_mask;         /* bit per item index */
} EnchantDef;

typedef struct {
    const char *id;
    const char *display;
} ItemDef;

extern ItemDef g_items[];
extern const int g_item_count;

extern EnchantDef g_enchants[];
extern const int g_enchant_count;

void enchant_data_init(void);

int item_index_by_id(const char *id);
int enchant_index_by_id(const char *id);

/* All enchant indices applicable to the given item index. If item is the
 * special "book" item, every enchant applies. Returns count written into out
 * (out must hold at least g_enchant_count entries). */
int enchants_for_item(int item_index, int is_book, int *out);

/* Group of mutually-incompatible enchant indices reachable (transitively)
 * from `enchant_index`, restricted to `allowed_mask` (enchants applicable to
 * the current item). Mirrors incompatibleGroupFromNamespace() in script.js. */
uint64_t incompatible_group(int enchant_index, uint64_t allowed_mask);

#endif
