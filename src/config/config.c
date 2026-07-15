/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Strict typed settings: schema, migration, quarantine, durable replace.

#include <anoptic_config.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include <jsmn.h>

#define CONFIG_TOKENS 48

typedef struct parsed_config {
    ano_config value;
    uint32_t version;
} parsed_config;


/* Parse */

static bool tok_eq(const char *json, const jsmntok_t *tok, const char *text)
{
    size_t n = strlen(text);
    return tok->type == JSMN_STRING && (size_t)(tok->end - tok->start) == n
        && memcmp(json + tok->start, text, n) == 0;
}

static int tok_next(const jsmntok_t *tokens, int count, int at)
{
    int end = tokens[at].end;
    at++;
    while (at < count && tokens[at].start < end)
        at++;
    return at;
}

static bool tok_u32(const char *json, const jsmntok_t *tok, uint32_t *out)
{
    if (tok->type != JSMN_PRIMITIVE || tok->end <= tok->start || tok->end - tok->start >= 16)
        return false;
    char tmp[16];
    size_t n = (size_t)(tok->end - tok->start);
    memcpy(tmp, json + tok->start, n);
    tmp[n] = '\0';
    char *end = NULL;
    unsigned long v = strtoul(tmp, &end, 10);
    if (end == tmp || *end != '\0' || v > UINT32_MAX)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool tok_float(const char *json, const jsmntok_t *tok, float *out)
{
    if (tok->type != JSMN_PRIMITIVE || tok->end <= tok->start || tok->end - tok->start >= 32)
        return false;
    char tmp[32];
    size_t n = (size_t)(tok->end - tok->start);
    memcpy(tmp, json + tok->start, n);
    tmp[n] = '\0';
    char *end = NULL;
    float v = strtof(tmp, &end);
    if (end == tmp || *end != '\0' || !isfinite(v))
        return false;
    *out = v;
    return true;
}

static bool tok_bool(const char *json, const jsmntok_t *tok, bool *out)
{
    if (tok->type != JSMN_PRIMITIVE)
        return false;
    size_t n = (size_t)(tok->end - tok->start);
    if (n == 4 && memcmp(json + tok->start, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (n == 5 && memcmp(json + tok->start, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_camera(const char *json, const jsmntok_t *tokens, int count, int object,
                         ano_config *config)
{
    if (tokens[object].type != JSMN_OBJECT)
        return false;
    bool move = false, look = false;
    for (int i = object + 1; i < count && tokens[i].start < tokens[object].end; ) {
        if (tokens[i].parent != object || i + 1 >= count)
            return false;
        int value = i + 1;
        if (tok_eq(json, &tokens[i], "move_speed"))
            move = !move && tok_float(json, &tokens[value], &config->camera_move_speed);
        else if (tok_eq(json, &tokens[i], "look_sensitivity"))
            look = !look && tok_float(json, &tokens[value], &config->camera_look_sensitivity);
        else
            return false;
        i = tok_next(tokens, count, value);
    }
    return move && look;
}

static bool parse_ui(const char *json, const jsmntok_t *tokens, int count, int object,
                     ano_config *config)
{
    if (tokens[object].type != JSMN_OBJECT)
        return false;
    bool menu = false;
    for (int i = object + 1; i < count && tokens[i].start < tokens[object].end; ) {
        if (tokens[i].parent != object || i + 1 >= count)
            return false;
        int value = i + 1;
        if (tok_eq(json, &tokens[i], "menu_at_start"))
            menu = !menu && tok_bool(json, &tokens[value], &config->menu_at_start);
        else
            return false;
        i = tok_next(tokens, count, value);
    }
    return menu;
}

static bool parse_config(const char *json, size_t len, parsed_config *out)
{
    jsmn_parser parser;
    jsmntok_t tokens[CONFIG_TOKENS];
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, len, tokens, CONFIG_TOKENS);
    if (count < 1 || tokens[0].type != JSMN_OBJECT)
        return false;
    for (size_t i = (size_t)tokens[0].end; i < len; i++)
        if (json[i] != ' ' && json[i] != '\t' && json[i] != '\r' && json[i] != '\n')
            return false;

    ano_config config = {0};
    uint32_t version = 0;
    bool schema = false, have_version = false, camera = false, ui = false;
    bool old_move = false, old_look = false, old_menu = false;
    for (int i = 1; i < count && tokens[i].start < tokens[0].end; ) {
        if (tokens[i].parent != 0 || i + 1 >= count || tokens[i].type != JSMN_STRING)
            return false;
        int value = i + 1;
        if (tok_eq(json, &tokens[i], "schema"))
            schema = !schema && tok_eq(json, &tokens[value], "anoptic.settings");
        else if (tok_eq(json, &tokens[i], "version"))
            have_version = !have_version && tok_u32(json, &tokens[value], &version);
        else if (tok_eq(json, &tokens[i], "camera"))
            camera = !camera && parse_camera(json, tokens, count, value, &config);
        else if (tok_eq(json, &tokens[i], "ui"))
            ui = !ui && parse_ui(json, tokens, count, value, &config);
        else if (tok_eq(json, &tokens[i], "move_speed"))
            old_move = !old_move && tok_float(json, &tokens[value], &config.camera_move_speed);
        else if (tok_eq(json, &tokens[i], "look_sensitivity"))
            old_look = !old_look && tok_float(json, &tokens[value], &config.camera_look_sensitivity);
        else if (tok_eq(json, &tokens[i], "menu_at_start"))
            old_menu = !old_menu && tok_bool(json, &tokens[value], &config.menu_at_start);
        else
            return false;
        i = tok_next(tokens, count, value);
    }
    if (!schema || !have_version)
        return false;
    if (version == 1) {
        if (!old_move || !old_look || !old_menu || camera || ui)
            return false;
    } else if (version == ANO_CONFIG_VERSION) {
        if (!camera || !ui || old_move || old_look || old_menu)
            return false;
    } else {
        return false;
    }
    if (!ano_config_validate(&config))
        return false;
    out->value = config;
    out->version = version;
    return true;
}


/* Persistence */

void ano_config_defaults(ano_config *config)
{
    if (config == NULL)
        return;
    *config = (ano_config){
        .camera_move_speed = 2.5f,
        .camera_look_sensitivity = 0.003f,
        .menu_at_start = false,
    };
}

bool ano_config_validate(const ano_config *config)
{
    return config != NULL && isfinite(config->camera_move_speed)
        && config->camera_move_speed >= 0.05f && config->camera_move_speed <= 100.0f
        && isfinite(config->camera_look_sensitivity)
        && config->camera_look_sensitivity >= 0.00001f
        && config->camera_look_sensitivity <= 0.1f;
}

int ano_config_save(const ano_config *config)
{
    if (!ano_config_validate(config))
        return -1;
    char json[384];
    int n = snprintf(json, sizeof json,
                     "{\"schema\":\"anoptic.settings\",\"version\":2,"
                     "\"camera\":{\"move_speed\":%.9g,\"look_sensitivity\":%.9g},"
                     "\"ui\":{\"menu_at_start\":%s}}\n",
                     (double)config->camera_move_speed,
                     (double)config->camera_look_sensitivity,
                     config->menu_at_start ? "true" : "false");
    if (n <= 0 || n >= (int)sizeof json)
        return -1;
    return ano_res_write(ANO_CONFIG_PATH, json, (size_t)n);
}

ano_config_status ano_config_load(ano_res_lifetime lifetime, ano_config *config)
{
    if (config == NULL || lifetime.kind != ANO_RES_LIFETIME_SAVE_CONFIG)
        return ANO_CONFIG_INVALID_ARGUMENT;
    anores_t resource = ano_res_get(lifetime, ANO_CONFIG_PATH);
    if (resource.gen == 0) {
        (void)ano_res_quarantine(ANO_CONFIG_PATH);
        ano_config_defaults(config);
        return ano_config_save(config) == 0 ? ANO_CONFIG_DEFAULTED : ANO_CONFIG_IO_ERROR;
    }

    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    parsed_config parsed = {0};
    bool registered = ano_res_reader_register(&reader) == 0;
    bool begun = registered && ano_res_read_begin(&reader, &read) == 0;
    bool valid = false;
    if (begun) {
        anostr_t bytes = ano_res_bytes(&read, resource);
        valid = parse_config(anostr_bytes(&bytes), anostr_len(bytes), &parsed);
        ano_res_read_end(&read);
    }
    if (registered)
        (void)ano_res_reader_unregister(&reader);
    (void)ano_res_unload(lifetime, resource);

    if (!valid) {
        (void)ano_res_quarantine(ANO_CONFIG_PATH);
        ano_config_defaults(config);
        return ano_config_save(config) == 0 ? ANO_CONFIG_DEFAULTED : ANO_CONFIG_IO_ERROR;
    }
    *config = parsed.value;
    if (parsed.version != ANO_CONFIG_VERSION)
        return ano_config_save(config) == 0 ? ANO_CONFIG_MIGRATED : ANO_CONFIG_IO_ERROR;
    return ANO_CONFIG_LOADED;
}
