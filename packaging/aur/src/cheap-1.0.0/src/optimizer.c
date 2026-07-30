#include "optimizer.h"
#include <stdlib.h>
#include <string.h>

/*
 * This is a line-by-line port of work.js from the original enchant-order
 * project (https://github.com/iamcal/enchant-order), rewritten around a
 * bitmask dynamic-programming scheme instead of JS's memoized recursion
 * over object hashes. Function-level correspondence:
 *
 *   experience()                  -> experience_for_level()
 *   MergeEnchants constructor     -> merge_two()
 *   cheapestItemFromItems2()      -> cheapest_pair()
 *   cheapestItemsFromList()       -> get_cheapest_map()  (case 1/2/default)
 *   cheapestItemsFromListN()      -> decompose_and_combine()
 *   cheapestItemsFromDictionaries2() -> combine_maps()
 *   compareCheapest()             -> pick_cheaper()
 *   removeExpensiveCandidatesFromDictionary() -> workmap_prune()
 *   process()                     -> optimize() / run_dp()
 *   getInstructions()             -> collect_instructions()
 *
 * The recursive memoization in cheapestItemsFromList() is keyed in the
 * original by a hash of the item list; here the DP's working set never
 * changes identity across a call, so a bitmask over "which of the m items
 * assembled for the DP are included" is an exact, cheaper substitute.
 */

#define MAX_WORK 16

typedef struct {
    ItemState *by_work[MAX_WORK + 1];
} WorkMap;

typedef struct {
    ItemState **items;
    int count;
    int cap;
} Arena;

static ItemState *arena_alloc(Arena *a) {
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 256;
        a->items = realloc(a->items, sizeof(ItemState *) * (size_t)a->cap);
    }
    ItemState *node = calloc(1, sizeof(ItemState));
    a->items[a->count++] = node;
    return node;
}

static void arena_free(Arena *a) {
    for (int i = 0; i < a->count; i++) free(a->items[i]);
    free(a->items);
    free(a);
}

typedef struct MemoNode {
    uint64_t key;
    WorkMap map;
    struct MemoNode *next;
} MemoNode;

typedef struct {
    MemoNode **buckets;
    int bucket_count;
} MemoTable;

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static void memo_init(MemoTable *t, int item_count) {
    int buckets = 1024;
    int want = item_count < 20 ? (1 << item_count) : (1 << 20);
    while (buckets < want) buckets <<= 1;
    t->bucket_count = buckets;
    t->buckets = calloc((size_t)buckets, sizeof(MemoNode *));
}

static void memo_free(MemoTable *t) {
    for (int i = 0; i < t->bucket_count; i++) {
        MemoNode *n = t->buckets[i];
        while (n) {
            MemoNode *next = n->next;
            free(n);
            n = next;
        }
    }
    free(t->buckets);
}

static WorkMap *memo_get(MemoTable *t, uint64_t key) {
    unsigned idx = (unsigned)(mix64(key) & (uint64_t)(t->bucket_count - 1));
    for (MemoNode *n = t->buckets[idx]; n; n = n->next) {
        if (n->key == key) return &n->map;
    }
    return NULL;
}

static WorkMap *memo_put(MemoTable *t, uint64_t key, const WorkMap *map) {
    unsigned idx = (unsigned)(mix64(key) & (uint64_t)(t->bucket_count - 1));
    MemoNode *n = malloc(sizeof(MemoNode));
    n->key = key;
    n->map = *map;
    n->next = t->buckets[idx];
    t->buckets[idx] = n;
    return &n->map;
}

typedef struct {
    ItemState **items;
    int m;
    Arena *arena;
    MemoTable memo;
} DPContext;

long experience_for_level(long level) {
    if (level <= 0) return 0;
    double lv = (double)level;
    double xp;
    if (level <= 16) {
        xp = lv * lv + 6 * lv;
    } else if (level <= 31) {
        xp = 2.5 * lv * lv - 40.5 * lv + 360.0;
    } else {
        xp = 4.5 * lv * lv - 162.5 * lv + 2220.0;
    }
    return (long)(xp + 0.5);
}

static ItemState *merge_two(Arena *arena, ItemState *left, ItemState *right) {
    long merge_cost = right->value + ((1L << left->work) - 1) + ((1L << right->work) - 1);
    if (merge_cost > MAXIMUM_MERGE_LEVELS) return NULL;

    ItemState *node = arena_alloc(arena);
    node->is_leaf = 0;
    node->is_target_item = left->is_target_item || right->is_target_item;
    node->enchant_index = -1;
    node->value = left->value + right->value;
    node->work = (left->work > right->work ? left->work : right->work) + 1;
    node->xp = left->xp + right->xp + experience_for_level(merge_cost);
    node->enchant_mask = left->enchant_mask | right->enchant_mask;
    node->left = left;
    node->right = right;
    node->merge_cost = merge_cost;
    return node;
}

/* Mirrors compareCheapest() when both candidates are already known to share
 * the same work value (the only case the original ever resolves to a single
 * winner instead of keeping both). */
static ItemState *pick_cheaper(ItemState *a, ItemState *b) {
    if (a->value != b->value) return a->value < b->value ? a : b;
    return a->xp <= b->xp ? a : b;
}

/* Mirrors cheapestItemFromItems2(): the item carrying the target's identity
 * must always end up as the "left" (persisting) operand of MergeEnchants,
 * since only the sacrificed side's value feeds the merge cost. When neither
 * operand is the target, try both orders and keep the cheaper one. */
static ItemState *cheapest_pair(Arena *arena, ItemState *a, ItemState *b) {
    if (a->is_target_item) return merge_two(arena, a, b);
    if (b->is_target_item) return merge_two(arena, b, a);

    ItemState *normal = merge_two(arena, a, b);
    ItemState *reversed = merge_two(arena, b, a);
    if (!normal) return reversed;
    if (!reversed) return normal;
    return pick_cheaper(normal, reversed);
}

static void workmap_insert(WorkMap *m, ItemState *item) {
    if (!item) return;
    int w = item->work;
    if (w > MAX_WORK) return; /* astronomically over the level cap already; unreachable in practice */
    m->by_work[w] = m->by_work[w] ? pick_cheaper(m->by_work[w], item) : item;
}

/* Mirrors removeExpensiveCandidatesFromDictionary(): keep only Pareto-optimal
 * (work, value) pairs, iterating work ascending like JS's integer-key object
 * iteration order. */
static void workmap_prune(WorkMap *m) {
    int have_value = 0;
    long cheapest_value = 0;
    for (int w = 0; w <= MAX_WORK; w++) {
        if (!m->by_work[w]) continue;
        long v = m->by_work[w]->value;
        if (have_value && v >= cheapest_value) {
            m->by_work[w] = NULL;
        } else {
            cheapest_value = v;
            have_value = 1;
        }
    }
}

/* Mirrors cheapestItemsFromDictionaries2(). */
static WorkMap combine_maps(Arena *arena, const WorkMap *left, const WorkMap *right) {
    WorkMap result;
    memset(&result, 0, sizeof(result));
    for (int lw = 0; lw <= MAX_WORK; lw++) {
        if (!left->by_work[lw]) continue;
        for (int rw = 0; rw <= MAX_WORK; rw++) {
            if (!right->by_work[rw]) continue;
            ItemState *merged = cheapest_pair(arena, left->by_work[lw], right->by_work[rw]);
            workmap_insert(&result, merged);
        }
    }
    workmap_prune(&result);
    return result;
}

static WorkMap *get_cheapest_map(DPContext *ctx, uint64_t mask);

/* Mirrors cheapestItemsFromListN(): brute-force every way to split `mask`
 * into two non-empty groups (each combination tested at most once, by only
 * considering the half of the split that doesn't contain item 0 as
 * canonical when the two halves are equal size). */
static WorkMap decompose_and_combine(DPContext *ctx, uint64_t mask) {
    int k = __builtin_popcountll(mask);
    uint64_t lowest_bit = mask & (~mask + 1);
    WorkMap result;
    memset(&result, 0, sizeof(result));

    for (uint64_t sub = (mask - 1) & mask; sub != 0; sub = (sub - 1) & mask) {
        int sc = __builtin_popcountll(sub);
        if (sc > k / 2) continue;
        /* Only a truly self-complementary split (sc*2 == k, only possible for
         * even k) needs the dedup guard; for odd k, integer division would
         * otherwise make every sc == k/2 (e.g. k=3 -> k/2=1) and wrongly
         * drop 2 of every 3 splits. */
        if (sc * 2 == k && !(sub & lowest_bit)) continue;

        uint64_t comp = mask & ~sub;
        WorkMap *left = get_cheapest_map(ctx, sub);
        WorkMap *right = get_cheapest_map(ctx, comp);
        WorkMap combined = combine_maps(ctx->arena, left, right);

        for (int w = 0; w <= MAX_WORK; w++) {
            if (!combined.by_work[w]) continue;
            result.by_work[w] = result.by_work[w]
                ? pick_cheaper(result.by_work[w], combined.by_work[w])
                : combined.by_work[w];
        }
    }
    return result;
}

/* Mirrors cheapestItemsFromList() (memoized). */
static WorkMap *get_cheapest_map(DPContext *ctx, uint64_t mask) {
    WorkMap *cached = memo_get(&ctx->memo, mask);
    if (cached) return cached;

    WorkMap result;
    memset(&result, 0, sizeof(result));
    int k = __builtin_popcountll(mask);

    if (k == 1) {
        int idx = __builtin_ctzll(mask);
        workmap_insert(&result, ctx->items[idx]);
    } else if (k == 2) {
        int i0 = __builtin_ctzll(mask);
        uint64_t rest = mask & (mask - 1);
        int i1 = __builtin_ctzll(rest);
        workmap_insert(&result, cheapest_pair(ctx->arena, ctx->items[i0], ctx->items[i1]));
    } else {
        result = decompose_and_combine(ctx, mask);
    }

    return memo_put(&ctx->memo, mask, &result);
}

static void collect_instructions(ItemState *node, ItemState **list, int *count) {
    if (!node || node->is_leaf) return;
    collect_instructions(node->left, list, count);
    collect_instructions(node->right, list, count);
    list[(*count)++] = node;
}

static ItemState *make_enchant_leaf(Arena *arena, const EnchantSelection *sel, int sel_index) {
    ItemState *node = arena_alloc(arena);
    node->is_leaf = 1;
    node->is_target_item = 0;
    node->enchant_index = sel->enchant_index;
    node->enchant_level = sel->level;
    node->value = (long)sel->level * g_enchants[sel->enchant_index].weight;
    node->work = 0;
    node->xp = 0;
    node->enchant_mask = (1ull << sel_index);
    return node;
}

static ItemState *make_blank_target(Arena *arena) {
    ItemState *node = arena_alloc(arena);
    node->is_leaf = 1;
    node->is_target_item = 1;
    node->enchant_index = -1;
    return node;
}

static ItemState *make_book_trunk(Arena *arena, ItemState *top_leaf) {
    ItemState *node = arena_alloc(arena);
    node->is_leaf = 1;
    node->is_target_item = 1;
    node->enchant_index = top_leaf->enchant_index;
    node->enchant_level = top_leaf->enchant_level;
    node->value = top_leaf->value;
    node->enchant_mask = top_leaf->enchant_mask;
    return node;
}

static void run_dp(OptimizeResult *result, Arena *arena, ItemState **dp_items, int m, OptimizeMode mode) {
    DPContext ctx;
    ctx.items = dp_items;
    ctx.m = m;
    ctx.arena = arena;
    memo_init(&ctx.memo, m);

    uint64_t full_mask = (m >= 64) ? ~0ull : ((1ull << m) - 1);
    WorkMap *final_map = get_cheapest_map(&ctx, full_mask);

    ItemState *best = NULL;
    if (mode == OPTIMIZE_MODE_PRIOR_WORK) {
        for (int w = 0; w <= MAX_WORK; w++) {
            if (final_map->by_work[w]) { best = final_map->by_work[w]; break; }
        }
    } else {
        long best_xp = 0;
        for (int w = 0; w <= MAX_WORK; w++) {
            ItemState *cand = final_map->by_work[w];
            if (!cand) continue;
            if (!best || cand->xp < best_xp) { best = cand; best_xp = cand->xp; }
        }
    }

    memo_free(&ctx.memo);

    if (!best) {
        result->found = 0;
        return;
    }

    ItemState **steps = malloc(sizeof(ItemState *) * (size_t)m);
    int step_count = 0;
    collect_instructions(best, steps, &step_count);

    long total_levels = 0;
    for (int i = 0; i < step_count; i++) total_levels += steps[i]->merge_cost;

    result->steps = steps;
    result->step_count = step_count;
    result->result = best;
    result->total_xp = best->xp;
    result->total_levels = total_levels;
    result->found = 1;
}

OptimizeResult *optimize(int item_index, int is_book,
                          const EnchantSelection *selections, int n,
                          OptimizeMode mode) {
    (void)item_index; /* the optimizer's math is item-agnostic; callers keep
                        * this for display purposes only */
    OptimizeResult *result = calloc(1, sizeof(OptimizeResult));
    Arena *arena = calloc(1, sizeof(Arena));
    result->_arena = arena;

    if (n <= 0) {
        result->found = 0;
        return result;
    }

    ItemState **leaves = malloc(sizeof(ItemState *) * (size_t)n);
    for (int i = 0; i < n; i++) leaves[i] = make_enchant_leaf(arena, &selections[i], i);

    int most_expensive = 0;
    for (int i = 1; i < n; i++) {
        if (leaves[i]->value > leaves[most_expensive]->value) most_expensive = i;
    }

    int *remaining = malloc(sizeof(int) * (size_t)n);
    int remaining_count = 0;
    for (int i = 0; i < n; i++) {
        if (i != most_expensive) remaining[remaining_count++] = i;
    }

    ItemState *trunk;
    ItemState *second_leaf = NULL;

    if (is_book) {
        trunk = make_book_trunk(arena, leaves[most_expensive]);

        int second_pos = 0;
        for (int i = 1; i < remaining_count; i++) {
            if (leaves[remaining[i]]->value > leaves[remaining[second_pos]]->value) second_pos = i;
        }
        second_leaf = leaves[remaining[second_pos]];
        for (int i = second_pos; i < remaining_count - 1; i++) remaining[i] = remaining[i + 1];
        remaining_count--;
    } else {
        trunk = make_blank_target(arena);
        second_leaf = leaves[most_expensive];
    }

    ItemState *merged_item = cheapest_pair(arena, trunk, second_leaf);
    if (!merged_item) {
        result->found = 0;
        free(leaves);
        free(remaining);
        return result;
    }

    int m = remaining_count + 1;
    ItemState **dp_items = malloc(sizeof(ItemState *) * (size_t)m);
    for (int i = 0; i < remaining_count; i++) dp_items[i] = leaves[remaining[i]];
    dp_items[remaining_count] = merged_item;

    run_dp(result, arena, dp_items, m, mode);

    free(dp_items);
    free(leaves);
    free(remaining);
    return result;
}

void optimize_result_free(OptimizeResult *r) {
    if (!r) return;
    free(r->steps);
    if (r->_arena) arena_free((Arena *)r->_arena);
    free(r);
}
