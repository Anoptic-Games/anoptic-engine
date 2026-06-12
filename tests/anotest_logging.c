/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_logging.h>

int main() {

    if (ano_log_init() != 0)
        return 1;

    int status = ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "Lol. Lmao even.");

    ano_log_cleanup();

    return status;
}
