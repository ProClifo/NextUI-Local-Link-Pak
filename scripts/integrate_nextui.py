#!/usr/bin/env python3
"""Integrate NextUI Local Link support into a clean NextUI checkout.

This deliberately uses exact source anchors instead of a long-lived patch with fragile
line numbers. If upstream NextUI changes an anchor, the script exits with an error rather
than silently producing a half-patched MinArch build.

Usage:
    python3 scripts/integrate_nextui.py /path/to/NextUI

The operation is idempotent: running it again on an already integrated checkout is safe.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


class IntegrationError(RuntimeError):
    pass


def replace_once(path: Path, old: str, new: str, marker: str | None = None) -> None:
    text = path.read_text()
    if marker and marker in text:
        return
    if old not in text:
        raise IntegrationError(f"anchor not found in {path}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1))


def insert_before(path: Path, anchor: str, addition: str, marker: str) -> None:
    text = path.read_text()
    if marker in text:
        return
    if anchor not in text:
        raise IntegrationError(f"anchor not found in {path}: {anchor[:100]!r}")
    path.write_text(text.replace(anchor, addition + anchor, 1))


def copy_if_changed(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if dst.exists() and dst.read_bytes() == data:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)


def integrate(nextui: Path, project: Path) -> None:
    ma = nextui / "workspace/all/minarch"
    if not (ma / "minarch.c").exists():
        raise IntegrationError(f"{nextui} does not look like a NextUI checkout")

    # Source files and ABI shared with the custom core.
    copy_if_changed(project / "integration/nextui/minarch_local_link.c", ma / "minarch_local_link.c")
    copy_if_changed(project / "integration/nextui/minarch_local_link.h", ma / "minarch_local_link.h")
    copy_if_changed(project / "include/local_link_abi.h", ma / "local_link_abi.h")

    # Ensure the MinArch helper is compiled.
    makefile = ma / "makefile"
    replace_once(
        makefile,
        "ma_options.c ma_frontend_opts.c ma_saves.c ma_video.c ma_core.c ma_game.c ma_environment.c ma_config.c ma_menu.c ma_runframe.c \\\n",
        "ma_options.c ma_frontend_opts.c ma_saves.c ma_video.c ma_core.c ma_game.c ma_environment.c ma_config.c ma_menu.c ma_runframe.c minarch_local_link.c \\\n",
        marker="minarch_local_link.c",
    )

    # Optional extension pointers in MinArch's Core object.
    internal = ma / "ma_internal.h"
    core_fields = """
\t/* Optional NextUI Local Link extension. Ordinary libretro cores leave these NULL. */
\tunsigned (*local_link_get_abi_version)(void);
\tuint64_t (*local_link_get_capabilities)(void);
\tuint32_t (*local_link_get_loaded_mask)(void);
\tunsigned (*local_link_get_active_instance)(void);
\tbool (*local_link_set_active_instance)(unsigned slot);
\tbool (*local_link_add_instance)(unsigned slot, const struct retro_game_info *game);
\tbool (*local_link_remove_instance)(unsigned slot);
\tvoid *(*local_link_get_memory_data)(unsigned slot, unsigned id);
\tsize_t (*local_link_get_memory_size)(unsigned slot, unsigned id);
\tsize_t (*local_link_serialize_size)(unsigned slot);
\tbool (*local_link_serialize)(unsigned slot, void *data, size_t size);
\tbool (*local_link_unserialize)(unsigned slot, const void *data, size_t size);
"""
    replace_once(
        internal,
        "\tsize_t (*get_memory_size)(unsigned id);\n",
        "\tsize_t (*get_memory_size)(unsigned id);\n" + core_fields,
        marker="local_link_get_abi_version",
    )

    core = ma / "ma_core.c"
    replace_once(
        core,
        '#include "ma_core.h"\n',
        '#include "ma_core.h"\n#include "minarch_local_link.h"\n',
        marker='#include "minarch_local_link.h"',
    )
    dlsyms = """
\t/* Optional Local Link ABI. dlsym returns NULL for ordinary cores. */
\tcore.local_link_get_abi_version = dlsym(core.handle, "retro_local_link_get_abi_version");
\tcore.local_link_get_capabilities = dlsym(core.handle, "retro_local_link_get_capabilities");
\tcore.local_link_get_loaded_mask = dlsym(core.handle, "retro_local_link_get_loaded_mask");
\tcore.local_link_get_active_instance = dlsym(core.handle, "retro_local_link_get_active_instance");
\tcore.local_link_set_active_instance = dlsym(core.handle, "retro_local_link_set_active_instance");
\tcore.local_link_add_instance = dlsym(core.handle, "retro_local_link_add_instance");
\tcore.local_link_remove_instance = dlsym(core.handle, "retro_local_link_remove_instance");
\tcore.local_link_get_memory_data = dlsym(core.handle, "retro_local_link_get_memory_data");
\tcore.local_link_get_memory_size = dlsym(core.handle, "retro_local_link_get_memory_size");
\tcore.local_link_serialize_size = dlsym(core.handle, "retro_local_link_serialize_size");
\tcore.local_link_serialize = dlsym(core.handle, "retro_local_link_serialize");
\tcore.local_link_unserialize = dlsym(core.handle, "retro_local_link_unserialize");
"""
    replace_once(
        core,
        '\tcore.get_memory_size = dlsym(core.handle, "retro_get_memory_size");\n',
        '\tcore.get_memory_size = dlsym(core.handle, "retro_get_memory_size");\n' + dlsyms,
        marker='retro_local_link_get_abi_version',
    )
    replace_once(
        core,
        "\t\tSRAM_write();\n\t\tCheats_free();",
        "\t\tif (LLMinarch_supported()) LLMinarch_saveAll();\n\t\telse SRAM_write();\n\t\tCheats_free();",
        marker="LLMinarch_saveAll()",
    )

    # Named SRAM and state helpers let every physical GBA retain NextUI-compatible
    # files under its own ROM name.
    saves_h = ma / "ma_saves.h"
    replace_once(
        saves_h,
        "void SRAM_read(void);\nvoid SRAM_write(void);\n",
        "void SRAM_read(void);\nvoid SRAM_write(void);\n"
        "int SRAM_readNamed(const char* alt_name, void* sram, size_t sram_size);\n"
        "int SRAM_writeNamed(const char* alt_name, const void* sram, size_t sram_size);\n",
        marker="SRAM_readNamed",
    )
    replace_once(
        saves_h,
        "int State_read(void);\nint State_write(void);\n",
        "int State_read(void);\nint State_write(void);\n"
        "int State_readNamed(const char* alt_name, int slot, void* state, size_t state_size);\n"
        "int State_writeNamed(const char* alt_name, int slot, const void* state, size_t state_size);\n",
        marker="State_readNamed",
    )

    saves_c = ma / "ma_saves.c"
    named_sram = r'''
static void LL_SRAM_getPathForName(const char* alt_name, char* filename) {
	char work_name[MAX_PATH];
	if (CFG_getSaveFormat() == SAVE_FORMAT_SRM || CFG_getSaveFormat() == SAVE_FORMAT_SRM_UNCOMPRESSED) {
		strcpy(work_name, alt_name);
		formatSavePath(work_name, filename, ".srm");
	}
	else if (CFG_getSaveFormat() == SAVE_FORMAT_GEN) {
		strcpy(work_name, alt_name);
		formatSavePath(work_name, filename, ".sav");
	}
	else sprintf(filename, "%s/%s.sav", core.saves_dir, alt_name);
}

int SRAM_readNamed(const char* alt_name, void* sram, size_t sram_size) {
	if (!alt_name || !*alt_name || !sram || !sram_size) return 0;
	char filename[MAX_PATH];
	LL_SRAM_getPathForName(alt_name, filename);
#ifdef HAS_SRM
	rzipstream_t* file = rzipstream_open(filename, RETRO_VFS_FILE_ACCESS_READ);
	if (!file) return 0;
	int64_t bytes = rzipstream_read(file, sram, sram_size);
	rzipstream_close(file);
	return bytes > 0;
#else
	FILE* file = fopen(filename, "r");
	if (!file) return 0;
	size_t bytes = fread(sram, 1, sram_size, file);
	fclose(file);
	return bytes > 0;
#endif
}

int SRAM_writeNamed(const char* alt_name, const void* sram, size_t sram_size) {
	if (!alt_name || !*alt_name || !sram || !sram_size) return 0;
	char filename[MAX_PATH];
	LL_SRAM_getPathForName(alt_name, filename);
	int ok = 0;
#ifdef HAS_SRM
	if (CFG_getSaveFormat() == SAVE_FORMAT_SRM) ok = rzipstream_write_file(filename, sram, sram_size);
	else ok = filestream_write_file(filename, sram, sram_size);
#else
	FILE* file = fopen(filename, "w");
	if (file) { ok = fwrite(sram, 1, sram_size, file) == sram_size; fclose(file); }
#endif
	if (ok) sync();
	return ok;
}

'''
    insert_before(saves_c, "void SRAM_read(void) {", named_sram, "LL_SRAM_getPathForName")

    named_state = r'''
static void LL_State_getPathForName(const char* alt_name, int slot, char* filename) {
	char work_name[MAX_PATH];
	if (CFG_getStateFormat() == STATE_FORMAT_SRM_EXTRADOT ||
	    CFG_getStateFormat() == STATE_FORMAT_SRM_UNCOMRESSED_EXTRADOT) {
		strcpy(work_name, alt_name);
		char* tmp = strrchr(work_name, '.');
		if (tmp != NULL && strlen(tmp) > 2 && strlen(tmp) <= 5) tmp[0] = '\0';
		if (slot == AUTO_RESUME_SLOT) sprintf(filename, "%s/%s.state.auto", core.states_dir, work_name);
		else sprintf(filename, "%s/%s.state.%i", core.states_dir, work_name, slot);
	}
	else if (CFG_getStateFormat() == STATE_FORMAT_SRM ||
	         CFG_getStateFormat() == STATE_FORMAT_SRM_UNCOMRESSED) {
		strcpy(work_name, alt_name);
		char* tmp = strrchr(work_name, '.');
		if (tmp != NULL && strlen(tmp) > 2 && strlen(tmp) <= 5) tmp[0] = '\0';
		if (slot == AUTO_RESUME_SLOT) sprintf(filename, "%s/%s.state.auto", core.states_dir, work_name);
		else if (slot == 0) sprintf(filename, "%s/%s.state", core.states_dir, work_name);
		else sprintf(filename, "%s/%s.state%i", core.states_dir, work_name, slot);
	}
	else sprintf(filename, "%s/%s.st%i", core.states_dir, alt_name, slot);
}

int State_readNamed(const char* alt_name, int slot, void* state, size_t state_size) {
	if (!alt_name || !*alt_name || !state || !state_size) return 0;
	char filename[MAX_PATH];
	LL_State_getPathForName(alt_name, slot, filename);
	uint8_t header[RASTATE_HEADER_SIZE] = {0};
	int success = 0;
#ifdef HAS_SRM
	rzipstream_t* file = rzipstream_open(filename, RETRO_VFS_FILE_ACCESS_READ);
	if (!file) return 0;
	if (rzipstream_read(file, header, sizeof(header)) < (int64_t)sizeof(header)) goto ll_state_read_done;
	if (memcmp(header, "RASTATE", 7) != 0) rzipstream_rewind(file);
	{ int64_t bytes = rzipstream_read(file, state, state_size); success = bytes > 0 && (size_t)bytes <= state_size; }
ll_state_read_done:
	rzipstream_close(file);
#else
	FILE* file = fopen(filename, "r");
	if (!file) return 0;
	if (fread(header, 1, sizeof(header), file) < sizeof(header)) goto ll_state_read_done;
	if (memcmp(header, "RASTATE", 7) != 0) fseek(file, 0, SEEK_SET);
	{ size_t bytes = fread(state, 1, state_size, file); success = bytes > 0 && bytes <= state_size; }
ll_state_read_done:
	fclose(file);
#endif
	return success;
}

int State_writeNamed(const char* alt_name, int slot, const void* state, size_t state_size) {
	if (!alt_name || !*alt_name || !state || !state_size) return 0;
	char filename[MAX_PATH];
	LL_State_getPathForName(alt_name, slot, filename);
	int success = 0;
#ifdef HAS_SRM
	if (CFG_getStateFormat() == STATE_FORMAT_SRM || CFG_getStateFormat() == STATE_FORMAT_SRM_EXTRADOT)
		success = rzipstream_write_file(filename, state, state_size);
	else success = filestream_write_file(filename, state, state_size);
#else
	FILE* file = fopen(filename, "w");
	if (file) { success = fwrite(state, 1, state_size, file) == state_size; fclose(file); }
#endif
	if (success) sync();
	return success;
}

'''
    insert_before(saves_c, "int State_read(void) {", named_state, "LL_State_getPathForName")

    # Emulator menu: inject Local Link actions before normal mGBA options.
    frontend = ma / "ma_frontend_opts.c"
    replace_once(
        frontend,
        '#include "notification.h"\n',
        '#include "notification.h"\n#include "minarch_local_link.h"\n',
        marker='#include "minarch_local_link.h"',
    )
    replace_once(
        frontend,
        "\tOptionEmulator_menu.items = calloc(cat_count + config.core.enabled_count + 1, sizeof(MenuItem));\n",
        "\tint local_link_count = 0;\n"
        "\tunsigned local_link_instances = 0;\n"
        "\tif (list->category == NULL && LLMinarch_supported()) {\n"
        "\t\tlocal_link_instances = LLMinarch_instanceCount();\n"
        "\t\tif (local_link_instances < 4) local_link_count++;\n"
        "\t\tif (local_link_instances > 1) local_link_count += 2;\n"
        "\t}\n\n"
        "\tOptionEmulator_menu.items = calloc(local_link_count + cat_count + config.core.enabled_count + 1, sizeof(MenuItem));\n\n"
        "\tint menu_offset = 0;\n"
        "\tif (local_link_count) {\n"
        "\t\tif (local_link_instances < 4) {\n"
        "\t\t\tMenuItem *item = &OptionEmulator_menu.items[menu_offset++];\n"
        "\t\t\titem->key = \"local_link_add\"; item->name = \"Add Instance\";\n"
        "\t\t\titem->desc = \"Link another GBA game on this device.\"; item->on_confirm = LLMinarch_menuAdd;\n"
        "\t\t}\n"
        "\t\tif (local_link_instances > 1) {\n"
        "\t\t\tMenuItem *item = &OptionEmulator_menu.items[menu_offset++];\n"
        "\t\t\titem->key = \"local_link_switch\"; item->name = \"Switch Instance\";\n"
        "\t\t\titem->desc = \"Show and control the next linked GBA.\"; item->on_confirm = LLMinarch_menuSwitch;\n"
        "\t\t\titem = &OptionEmulator_menu.items[menu_offset++];\n"
        "\t\t\titem->key = \"local_link_remove\"; item->name = \"Remove Instance\";\n"
        "\t\t\titem->desc = \"Disconnect the currently active GBA.\"; item->on_confirm = LLMinarch_menuRemove;\n"
        "\t\t}\n"
        "\t}\n",
        marker="local_link_count",
    )
    replace_once(frontend, "\t\tMenuItem *item = &OptionEmulator_menu.items[i];\n",
                 "\t\tMenuItem *item = &OptionEmulator_menu.items[menu_offset + i];\n",
                 marker="items[menu_offset + i]")
    replace_once(frontend, "\t\tMenuItem *item = &OptionEmulator_menu.items[cat_count + i];\n",
                 "\t\tMenuItem *item = &OptionEmulator_menu.items[menu_offset + cat_count + i];\n",
                 marker="items[menu_offset + cat_count + i]")
    replace_once(frontend, "\tif (cat_count || config.core.enabled_count) {\n",
                 "\tif (local_link_count || cat_count || config.core.enabled_count) {\n",
                 marker="local_link_count || cat_count")

    # Quit and Save & Quit act on the active physical GBA while peers remain.
    menu = ma / "ma_menu.c"
    replace_once(menu, '#include "ma_menu.h"\n', '#include "ma_menu.h"\n#include "minarch_local_link.h"\n',
                 marker='#include "minarch_local_link.h"')
    quit_anchor = """\t\t\t\tcase ITEM_QUIT:
\t\t\t\t\tstatus = STATUS_QUIT;
\t\t\t\t\tshow_menu = 0;
\t\t\t\t\tquit = 1; // TODO: tmp?
\t\t\t\tbreak;
"""
    quit_new = """\t\t\t\tcase ITEM_QUIT:
\t\t\t\t\tif (LLMinarch_supported() && LLMinarch_instanceCount() > 1) {
\t\t\t\t\t\tif (!LLMinarch_removeActive()) {
\t\t\t\t\t\t\tMenu_message("Could not save/disconnect this GBA.", (char*[]){ "B", "BACK", NULL });
\t\t\t\t\t\t\tbreak;
\t\t\t\t\t\t}
\t\t\t\t\t\tstatus = STATUS_CONT;
\t\t\t\t\t\tshow_menu = 0;
\t\t\t\t\t\tbreak;
\t\t\t\t\t}
\t\t\t\t\tstatus = STATUS_QUIT;
\t\t\t\t\tshow_menu = 0;
\t\t\t\t\tquit = 1; // TODO: tmp?
\t\t\t\tbreak;
"""
    replace_once(menu, quit_anchor, quit_new, marker="Could not save/disconnect this GBA")

    input_c = ma / "ma_input.c"
    replace_once(input_c, '#include "ma_input.h"\n', '#include "ma_input.h"\n#include "minarch_local_link.h"\n',
                 marker='#include "minarch_local_link.h"')
    savequit_anchor = """\t\t\t\t\tcase SHORTCUT_SAVE_QUIT:
\t\t\t\t\t\tnewScreenshot = 1;
\t\t\t\t\t\tquit = 1;
\t\t\t\t\t\tMenu_saveState();
\t\t\t\t\t\tbreak;
"""
    savequit_new = """\t\t\t\t\tcase SHORTCUT_SAVE_QUIT:
\t\t\t\t\t\tif (LLMinarch_supported() && LLMinarch_instanceCount() > 1) {
\t\t\t\t\t\t\tnewScreenshot = 1;
\t\t\t\t\t\t\tif (!LLMinarch_saveAndRemoveActive()) {
\t\t\t\t\t\t\t\tNotification_push(NOTIFICATION_SETTING, "Could not Save & Quit this GBA", NULL);
\t\t\t\t\t\t\t}
\t\t\t\t\t\t\tbreak;
\t\t\t\t\t\t}
\t\t\t\t\t\tnewScreenshot = 1;
\t\t\t\t\t\tquit = 1;
\t\t\t\t\t\tMenu_saveState();
\t\t\t\t\t\tbreak;
"""
    replace_once(input_c, savequit_anchor, savequit_new, marker="Could not Save & Quit this GBA")

    print(f"Local Link integration applied to {nextui}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("nextui", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    try:
        integrate(args.nextui.resolve(), project)
    except IntegrationError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
