/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_logging.h>

int main() {

    ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "Lol. Lmao even.");

    return 0;
}