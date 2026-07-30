/* Minimal CLI used only to diff the C optimizer against the JS reference
 * implementation via scripts/compare.sh. Not shipped in the GUI app. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enchant_data.h"
#include "optimizer.h"

int main(int argc, char **argv) {
    enchant_data_init();

    if (argc < 4) {
        fprintf(stderr, "usage: %s <item|book> <levels|prior_work> <enchant:level>...\n", argv[0]);
        return 1;
    }

    const char *item_arg = argv[1];
    const char *mode_arg = argv[2];

    int is_book = strcmp(item_arg, "book") == 0;
    int item_index = is_book ? -1 : item_index_by_id(item_arg);
    if (!is_book && item_index < 0) {
        fprintf(stderr, "unknown item: %s\n", item_arg);
        return 1;
    }

    OptimizeMode mode;
    if (strcmp(mode_arg, "levels") == 0) mode = OPTIMIZE_MODE_XP;
    else if (strcmp(mode_arg, "prior_work") == 0) mode = OPTIMIZE_MODE_PRIOR_WORK;
    else { fprintf(stderr, "unknown mode: %s\n", mode_arg); return 1; }

    int n = argc - 3;
    EnchantSelection *sel = malloc(sizeof(EnchantSelection) * (size_t)n);
    for (int i = 0; i < n; i++) {
        char buf[128];
        strncpy(buf, argv[3 + i], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *colon = strchr(buf, ':');
        if (!colon) { fprintf(stderr, "bad enchant spec: %s\n", argv[3 + i]); return 1; }
        *colon = 0;
        int idx = enchant_index_by_id(buf);
        if (idx < 0) { fprintf(stderr, "unknown enchant: %s\n", buf); return 1; }
        sel[i].enchant_index = idx;
        sel[i].level = atoi(colon + 1);
    }

    OptimizeResult *result = optimize(item_index, is_book, sel, n, mode);

    if (!result->found) {
        printf("{\"found\":false}\n");
        optimize_result_free(result);
        free(sel);
        return 0;
    }

    printf("{\"total_xp\":%ld,\"total_work_final\":%d,\"total_value_l\":%ld,",
           result->total_xp, result->result->work, result->result->value);
    printf("\"max_levels\":%ld,\"max_xp\":%ld,\"steps\":[",
           result->total_levels, experience_for_level(result->total_levels));
    for (int i = 0; i < result->step_count; i++) {
        ItemState *s = result->steps[i];
        long xp = experience_for_level(s->merge_cost);
        long prior_work_after = (1L << s->work) - 1;
        printf("%s[%ld,%ld,%ld]", i ? "," : "", s->merge_cost, xp, prior_work_after);
    }
    printf("]}\n");

    optimize_result_free(result);
    free(sel);
    return 0;
}
