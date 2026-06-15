/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_MAIN_H
#define ANO_MAIN_H

// Renderer interface — omitted in headless builds.
#ifndef HEADLESS_BUILD
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/instance/instanceInit.h"
#include "vulkan_backend/structs.h"
#endif

#endif
