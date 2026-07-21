/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
// Physical-key -> action-id bindings via resource namespace.
=======
// Physical-key to stable action-id bindings persisted through the resource namespace.
>>>>>>> block-b1-base

#ifndef ANOPTICENGINE_ANOPTIC_KEYBINDINGS_H
#define ANOPTICENGINE_ANOPTIC_KEYBINDINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "anoptic_resources.h"
#include "anoptic_strings.h"

#define ANO_KEYBINDINGS_VERSION 2u
#define ANO_KEYBINDINGS_PATH "config/keybindings.json"
#define ANO_KEYBINDING_COUNT 13u

<<<<<<< HEAD

/* Keys */

// Platform-neutral keys match GLFW physical-key tokens.
=======
// Platform-neutral key values deliberately match GLFW's stable logical-key values.
>>>>>>> block-b1-base
enum {
    ANO_KEY_SPACE = 32,
    ANO_KEY_APOSTROPHE = 39,
    ANO_KEY_SEMICOLON = 59,
    ANO_KEY_A = 65,
    ANO_KEY_D = 68,
    ANO_KEY_H = 72,
    ANO_KEY_L = 76,
    ANO_KEY_M = 77,
    ANO_KEY_S = 83,
    ANO_KEY_W = 87,
    ANO_KEY_LEFT_BRACKET = 91,
    ANO_KEY_RIGHT_BRACKET = 93,
    ANO_KEY_LEFT_CONTROL = 341,
};

<<<<<<< HEAD

/* Actions */

=======
>>>>>>> block-b1-base
#define ANO_ACTION_MOVE_FORWARD      ANOSTR_SID("move.forward")
#define ANO_ACTION_MOVE_BACKWARD     ANOSTR_SID("move.backward")
#define ANO_ACTION_MOVE_LEFT         ANOSTR_SID("move.left")
#define ANO_ACTION_MOVE_RIGHT        ANOSTR_SID("move.right")
#define ANO_ACTION_MOVE_UP           ANOSTR_SID("move.up")
#define ANO_ACTION_MOVE_DOWN         ANOSTR_SID("move.down")
#define ANO_ACTION_MENU_TOGGLE       ANOSTR_SID("menu.toggle")
#define ANO_ACTION_LIGHTING_CYCLE    ANOSTR_SID("render.lighting_cycle")
#define ANO_ACTION_LOD_FINER         ANOSTR_SID("render.lod_finer")
#define ANO_ACTION_LOD_COARSER       ANOSTR_SID("render.lod_coarser")
#define ANO_ACTION_SHADOW_LOD_FINER  ANOSTR_SID("render.shadow_lod_finer")
#define ANO_ACTION_SHADOW_LOD_COARSER ANOSTR_SID("render.shadow_lod_coarser")
#define ANO_ACTION_HIZ_TOGGLE        ANOSTR_SID("render.hiz_toggle")

<<<<<<< HEAD

/* Types */

=======
>>>>>>> block-b1-base
typedef struct ano_keybinding {
    anostr_sid action;
    int32_t key;
    int32_t mods;
} ano_keybinding;

typedef struct ano_keybindings {
    uint32_t count;
    ano_keybinding entries[ANO_KEYBINDING_COUNT];
} ano_keybindings;

typedef enum ano_keybindings_status {
    ANO_KEYBINDINGS_LOADED = 0,
    ANO_KEYBINDINGS_DEFAULTED,
    ANO_KEYBINDINGS_MIGRATED,
    ANO_KEYBINDINGS_IO_ERROR,
    ANO_KEYBINDINGS_INVALID_ARGUMENT,
} ano_keybindings_status;

<<<<<<< HEAD

/* Persistence and Lookup */

=======
>>>>>>> block-b1-base
void ano_keybindings_defaults(ano_keybindings *bindings);
bool ano_keybindings_validate(const ano_keybindings *bindings);
int ano_keybindings_save(const ano_keybindings *bindings);
ano_keybindings_status ano_keybindings_load(ano_res_lifetime lifetime,
                                             ano_keybindings *bindings);
anostr_sid ano_keybindings_action(const ano_keybindings *bindings, int32_t key, int32_t mods);
void ano_keybindings_install(const ano_keybindings *bindings);
anostr_sid ano_keybindings_current_action(int32_t key, int32_t mods);

#endif // ANOPTICENGINE_ANOPTIC_KEYBINDINGS_H
