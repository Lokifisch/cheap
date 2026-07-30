#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <stdint.h>
#include "enchant_data.h"

/* Ported from work.js (the WebWorker in the original enchant-order tool).
 * See optimizer.c for a detailed mapping between this and the original
 * JavaScript functions. */

#define MAXIMUM_MERGE_LEVELS 39

typedef enum {
    OPTIMIZE_MODE_XP,         /* "levels" in the original: minimize total XP spent */
    OPTIMIZE_MODE_PRIOR_WORK  /* minimize the final item's anvil-use penalty depth */
} OptimizeMode;

typedef struct {
    int enchant_index; /* index into g_enchants */
    int level;
} EnchantSelection;

typedef struct ItemState ItemState;
struct ItemState {
    int is_leaf;
    int is_target_item; /* true if this node's lineage carries the item being enchanted
                          * (as opposed to being a pure enchanted-book side branch) */

    /* valid only when is_leaf && !is_target_item: this leaf is a single enchant on a book */
    int enchant_index;
    int enchant_level;

    long value;           /* 'l' in the original: this node's own contributed merge value */
    int work;              /* 'w': anvil-use depth, drives the prior-work penalty */
    long xp;               /* 'x': total accumulated XP cost to reach this node */
    uint64_t enchant_mask; /* bit per user selection index; which selections this node carries */

    ItemState *left, *right; /* NULL for leaves */
    long merge_cost;         /* levels cost of this merge step; valid when left/right set */
};

typedef struct {
    int step_count;
    ItemState **steps; /* post-order list of merge nodes: the order to perform anvil work in */

    ItemState *result;
    long total_xp;      /* result->xp: the actual minimum XP cost */
    long total_levels;  /* sum of each step's merge_cost, informational (mirrors 'max_levels') */

    int found;

    /* internal allocation arena; do not touch */
    void *_arena;
} OptimizeResult;

/* item_index: target item (index into g_items), ignored when is_book is true.
 * selections/n: the user's chosen (enchant, level) pairs -- mirrors the "enchants"
 * array built by retrieveEnchantmentFoundation() in script.js. */
OptimizeResult *optimize(int item_index, int is_book,
                          const EnchantSelection *selections, int n,
                          OptimizeMode mode);

void optimize_result_free(OptimizeResult *r);

/* Exposed for the GUI to render the same numbers the original tool shows,
 * and for the CLI validator to compare against the JS reference. */
long experience_for_level(long level);

#endif
