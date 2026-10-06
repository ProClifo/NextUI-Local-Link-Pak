#include "ma_internal.h"
#include "local_link_picker.h"

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct LLPickerEntry {
    char *name;
    char *path;
};

static int is_gba(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".gba") == 0;
}

static int compare_entries(const void *a, const void *b) {
    const struct LLPickerEntry *ea = a;
    const struct LLPickerEntry *eb = b;
    return strcasecmp(ea->name, eb->name);
}

static void free_entries(struct LLPickerEntry *entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; ++i) {
        free(entries[i].name);
        free(entries[i].path);
    }
    free(entries);
}

static int load_entries(const char *directory, struct LLPickerEntry **out_entries, size_t *out_count) {
    DIR *dir = opendir(directory);
    if (!dir) return 0;

    struct LLPickerEntry *entries = NULL;
    size_t count = 0;
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' || !is_gba(ent->d_name)) continue;

        struct LLPickerEntry *next = realloc(entries, (count + 1) * sizeof(*entries));
        if (!next) {
            free_entries(entries, count);
            closedir(dir);
            return 0;
        }
        entries = next;
        memset(&entries[count], 0, sizeof(entries[count]));

        entries[count].name = strdup(ent->d_name);
        if (!entries[count].name) {
            free_entries(entries, count);
            closedir(dir);
            return 0;
        }

        size_t path_len = strlen(directory) + strlen(ent->d_name) + 2;
        entries[count].path = malloc(path_len);
        if (!entries[count].path) {
            free(entries[count].name);
            entries[count].name = NULL;
            free_entries(entries, count);
            closedir(dir);
            return 0;
        }
        snprintf(entries[count].path, path_len, "%s/%s", directory, ent->d_name);
        ++count;
    }
    closedir(dir);

    if (!count) {
        free(entries);
        return 0;
    }

    qsort(entries, count, sizeof(*entries), compare_entries);
    *out_entries = entries;
    *out_count = count;
    return 1;
}

int LLPicker_pickFirstRom(const char *directory, char *out_path, size_t out_size) {
    if (!directory || !*directory || !out_path || !out_size) return 0;

    struct LLPickerEntry *entries = NULL;
    size_t count = 0;
    if (!load_entries(directory, &entries, &count)) return 0;

    size_t selected = 0;
    PAD_reset();
    GFX_setMode(MODE_MENU);

    for (;;) {
        GFX_startFrame();
        PAD_poll();

        if (PAD_justPressed(BTN_DPAD_UP) || PAD_justRepeated(BTN_DPAD_UP)) {
            selected = selected == 0 ? count - 1 : selected - 1;
        }
        if (PAD_justPressed(BTN_DPAD_DOWN) || PAD_justRepeated(BTN_DPAD_DOWN)) {
            selected = (selected + 1) % count;
        }
        if (PAD_justPressed(BTN_A)) {
            snprintf(out_path, out_size, "%s", entries[selected].path);
            free_entries(entries, count);
            PAD_reset();
            return 1;
        }
        if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_MENU)) {
            free_entries(entries, count);
            PAD_reset();
            return 0;
        }

        GFX_clear(screen);

        char title[96];
        snprintf(title, sizeof(title), "Local Link\n\nChoose Player 1 (%zu/%zu)", selected + 1, count);
        GFX_blitMessage(font.medium, title, screen,
            &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING),
                        screen->w - SCALE1(2 * PADDING), SCALE1(80)});

        char selected_name[MAX_PATH];
        snprintf(selected_name, sizeof(selected_name), "%s", entries[selected].name);
        char *ext = strrchr(selected_name, '.');
        if (ext) *ext = '\0';
        GFX_blitMessage(font.large, selected_name, screen,
            &(SDL_Rect){SCALE1(PADDING), SCALE1(105),
                        screen->w - SCALE1(2 * PADDING), screen->h - SCALE1(155)});

        GFX_blitButtonGroup((char*[]){ "UP/DOWN", "CHOOSE", "A", "OPEN", "B", "BACK", NULL },
                            2, screen, 1);
        GFX_flip(screen);
    }
}
