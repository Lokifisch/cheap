#include "enchant_data.h"
#include <string.h>

/* Ported 1:1 from https://github.com/iamcal/enchant-order data.js */

ItemDef g_items[] = {
    {"helmet", "Helmet"},
    {"chestplate", "Chestplate"},
    {"leggings", "Leggings"},
    {"boots", "Boots"},
    {"turtle_shell", "Turtle Shell"},
    {"elytra", "Elytra"},
    {"sword", "Sword"},
    {"axe", "Axe"},
    {"mace", "Mace"},
    {"spear", "Spear"},
    {"trident", "Trident"},
    {"bow", "Bow"},
    {"crossbow", "Crossbow"},
    {"pickaxe", "Pickaxe"},
    {"shovel", "Shovel"},
    {"hoe", "Hoe"},
    {"shield", "Shield"},
    {"brush", "Brush"},
    {"fishing_rod", "Fishing Rod"},
    {"shears", "Shears"},
    {"flint_and_steel", "Flint and Steel"},
    {"carrot_on_a_stick", "Carrot on a Stick"},
    {"warped_fungus_on_a_stick", "Warped Fungus on a Stick"},
    {"pumpkin", "Pumpkin"},
    {"book", "Book"},
};
const int g_item_count = sizeof(g_items) / sizeof(g_items[0]);

#define ARMOR_ITEMS "helmet", "chestplate", "leggings", "boots", "turtle_shell"
#define TOOL_ITEMS "pickaxe", "shovel", "axe", "hoe"
#define MELEE_ITEMS "sword", "axe", "mace", "spear"
#define BREAKABLE_ITEMS \
    "helmet", "chestplate", "leggings", "boots", "pickaxe", "shovel", "axe", "sword", "hoe", \
    "brush", "fishing_rod", "bow", "shears", "flint_and_steel", "carrot_on_a_stick", \
    "warped_fungus_on_a_stick", "shield", "elytra", "trident", "turtle_shell", "crossbow", \
    "mace", "spear"

EnchantDef g_enchants[] = {
    {"protection", "Protection", 4, 1,
        {"blast_protection", "fire_protection", "projectile_protection"},
        {ARMOR_ITEMS}},
    {"aqua_affinity", "Aqua Affinity", 1, 2,
        {0},
        {"helmet", "turtle_shell"}},
    {"bane_of_arthropods", "Bane of Arthropods", 5, 1,
        {"smite", "sharpness", "density", "breach"},
        {MELEE_ITEMS}},
    {"blast_protection", "Blast Protection", 4, 2,
        {"fire_protection", "protection", "projectile_protection"},
        {ARMOR_ITEMS}},
    {"channeling", "Channeling", 1, 4,
        {"riptide"},
        {"trident"}},
    {"depth_strider", "Depth Strider", 3, 2,
        {"frost_walker"},
        {"boots"}},
    {"efficiency", "Efficiency", 5, 1,
        {0},
        {"pickaxe", "shovel", "axe", "hoe", "shears"}},
    {"feather_falling", "Feather Falling", 4, 1,
        {0},
        {"boots"}},
    {"fire_aspect", "Fire Aspect", 2, 2,
        {0},
        {"sword", "mace", "spear"}},
    {"fire_protection", "Fire Protection", 4, 1,
        {"blast_protection", "protection", "projectile_protection"},
        {ARMOR_ITEMS}},
    {"flame", "Flame", 1, 2,
        {0},
        {"bow"}},
    {"fortune", "Fortune", 3, 2,
        {"silk_touch"},
        {TOOL_ITEMS}},
    {"frost_walker", "Frost Walker", 2, 2,
        {"depth_strider"},
        {"boots"}},
    {"impaling", "Impaling", 5, 2,
        {0},
        {"trident"}},
    {"infinity", "Infinity", 1, 4,
        {"mending"},
        {"bow"}},
    {"knockback", "Knockback", 2, 1,
        {0},
        {"sword", "spear"}},
    {"looting", "Looting", 3, 2,
        {0},
        {"sword", "spear"}},
    {"loyalty", "Loyalty", 3, 1,
        {"riptide"},
        {"trident"}},
    {"luck_of_the_sea", "Luck of the Sea", 3, 2,
        {0},
        {"fishing_rod"}},
    {"lunge", "Lunge", 3, 1,
        {0},
        {"spear"}},
    {"lure", "Lure", 3, 2,
        {0},
        {"fishing_rod"}},
    {"mending", "Mending", 1, 2,
        {"infinity"},
        {BREAKABLE_ITEMS}},
    {"multishot", "Multishot", 1, 2,
        {"piercing"},
        {"crossbow"}},
    {"piercing", "Piercing", 4, 1,
        {"multishot"},
        {"crossbow"}},
    {"power", "Power", 5, 1,
        {0},
        {"bow"}},
    {"projectile_protection", "Projectile Protection", 4, 1,
        {"protection", "blast_protection", "fire_protection"},
        {ARMOR_ITEMS}},
    {"punch", "Punch", 2, 2,
        {0},
        {"bow"}},
    {"quick_charge", "Quick Charge", 3, 1,
        {0},
        {"crossbow"}},
    {"respiration", "Respiration", 3, 2,
        {0},
        {"helmet", "turtle_shell"}},
    {"riptide", "Riptide", 3, 2,
        {"channeling", "loyalty"},
        {"trident"}},
    {"sharpness", "Sharpness", 5, 1,
        {"bane_of_arthropods", "smite"},
        {"sword", "axe", "spear"}},
    {"silk_touch", "Silk Touch", 1, 4,
        {"fortune"},
        {TOOL_ITEMS}},
    {"smite", "Smite", 5, 1,
        {"bane_of_arthropods", "sharpness", "density", "breach"},
        {MELEE_ITEMS}},
    {"soul_speed", "Soul Speed", 3, 4,
        {0},
        {"boots"}},
    {"sweeping", "Sweeping Edge", 3, 2,
        {0},
        {"sword"}},
    {"swift_sneak", "Swift Sneak", 3, 4,
        {0},
        {"leggings"}},
    {"thorns", "Thorns", 3, 4,
        {0},
        {ARMOR_ITEMS}},
    {"unbreaking", "Unbreaking", 3, 1,
        {0},
        {BREAKABLE_ITEMS}},
    {"binding_curse", "Curse of Binding", 1, 4,
        {0},
        {"helmet", "chestplate", "leggings", "boots", "elytra", "pumpkin", "turtle_shell"}},
    {"vanishing_curse", "Curse of Vanishing", 1, 4,
        {0},
        {BREAKABLE_ITEMS}},
    {"density", "Density", 5, 1,
        {"breach", "smite", "bane_of_arthropods"},
        {"mace"}},
    {"breach", "Breach", 4, 2,
        {"density", "smite", "bane_of_arthropods"},
        {"mace"}},
    {"wind_burst", "Wind Burst", 3, 2,
        {0},
        {"mace"}},
};
const int g_enchant_count = sizeof(g_enchants) / sizeof(g_enchants[0]);

int item_index_by_id(const char *id) {
    for (int i = 0; i < g_item_count; i++) {
        if (strcmp(g_items[i].id, id) == 0) return i;
    }
    return -1;
}

int enchant_index_by_id(const char *id) {
    for (int i = 0; i < g_enchant_count; i++) {
        if (strcmp(g_enchants[i].id, id) == 0) return i;
    }
    return -1;
}

void enchant_data_init(void) {
    for (int i = 0; i < g_enchant_count; i++) {
        EnchantDef *e = &g_enchants[i];

        e->item_mask = 0;
        for (int j = 0; j < MAX_ENCHANT_ITEMS && e->item_ids[j]; j++) {
            int idx = item_index_by_id(e->item_ids[j]);
            if (idx >= 0) e->item_mask |= (1u << idx);
        }

        e->incompatible_mask = 0;
        for (int j = 0; j < MAX_ENCHANT_INCOMPATIBLE && e->incompatible_ids[j]; j++) {
            int idx = enchant_index_by_id(e->incompatible_ids[j]);
            if (idx >= 0) e->incompatible_mask |= (1ull << idx);
        }
    }
}

int enchants_for_item(int item_index, int is_book, int *out) {
    int count = 0;
    for (int i = 0; i < g_enchant_count; i++) {
        if (is_book || (g_enchants[i].item_mask & (1u << item_index))) {
            out[count++] = i;
        }
    }
    return count;
}

uint64_t incompatible_group(int enchant_index, uint64_t allowed_mask) {
    uint64_t group = 0;
    uint64_t queue = (1ull << enchant_index);

    while (queue) {
        int idx = __builtin_ctzll(queue);
        queue &= ~(1ull << idx);

        if (group & (1ull << idx)) continue;
        group |= (1ull << idx);

        uint64_t new_incompatible = g_enchants[idx].incompatible_mask & allowed_mask & ~group;
        queue |= new_incompatible;
    }

    return group;
}
