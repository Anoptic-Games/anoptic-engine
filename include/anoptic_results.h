/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_results.h
 * @brief Typed, domain-local outcomes for fallible public operations.
 *
 * Not a scalar. Inspect .code. Domains own their codes; success is zero.
 *
 * Example:
 * @code
 * ANO_RESULT_TYPE(AnoRenderSubmitResult,
 *     ANO_RENDER_SUBMIT_ACCEPTED = 0,
 *     ANO_RENDER_SUBMIT_BACKPRESSURE,
 *     ANO_RENDER_SUBMIT_OOM,
 *     ANO_RENDER_SUBMIT_INVALID
 * );
 *
 * AnoRenderSubmitResult result = ano_render_text_set(bridge, text_id, instances, count);
 *
 * switch (result.code)
 * {
 *     case ANO_RENDER_SUBMIT_ACCEPTED:
 *         break;
 *     case ANO_RENDER_SUBMIT_BACKPRESSURE:
 *         retry_later();
 *         break;
 *     case ANO_RENDER_SUBMIT_OOM:
 *         shed_optional_work();
 *         break;
 *     case ANO_RENDER_SUBMIT_INVALID:
 *         reject_submission();
 *         break;
 * }
 * @endcode
 */

#ifndef ANOPTIC_RESULTS_H
#define ANOPTIC_RESULTS_H

// Defines a nodiscard result wrapper and its domain-local code enum.
#define ANO_RESULT_TYPE(name, ...)                          \
    typedef enum name##Code { __VA_ARGS__ } name##Code;     \
    typedef struct [[nodiscard]] name {                     \
        name##Code code;                                    \
    } name

// Constructs name from one of its domain-local codes.
#define ANO_RESULT(name, value) ((name){ .code = (value) })

#endif // ANOPTIC_RESULTS_H
