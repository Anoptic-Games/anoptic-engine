/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
// Typed engine settings persisted via the resource namespace.
=======
// Typed engine settings persisted through the resource namespace.
>>>>>>> block-b1-base

#ifndef ANOPTICENGINE_ANOPTIC_CONFIG_H
#define ANOPTICENGINE_ANOPTIC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "anoptic_resources.h"

#define ANO_CONFIG_VERSION 2u
#define ANO_CONFIG_PATH "config/settings.json"

<<<<<<< HEAD

/* Types */

=======
>>>>>>> block-b1-base
typedef struct ano_config {
    float camera_move_speed;
    float camera_look_sensitivity;
    bool menu_at_start;
} ano_config;

typedef enum ano_config_status {
    ANO_CONFIG_LOADED = 0,
    ANO_CONFIG_DEFAULTED,
    ANO_CONFIG_MIGRATED,
    ANO_CONFIG_IO_ERROR,
    ANO_CONFIG_INVALID_ARGUMENT,
} ano_config_status;

<<<<<<< HEAD

/* Persistence */

=======
>>>>>>> block-b1-base
void ano_config_defaults(ano_config *config);
bool ano_config_validate(const ano_config *config);
int ano_config_save(const ano_config *config);
ano_config_status ano_config_load(ano_res_lifetime lifetime, ano_config *config);

#endif // ANOPTICENGINE_ANOPTIC_CONFIG_H
