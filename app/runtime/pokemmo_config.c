/* PokeMMO's PS5-owned graphics preferences. SPDX-License-Identifier: MIT */
#include "pokemmo_config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_CAPACITY (64u * 1024u)

struct client_setting
{
    const char *key;
    const char *value;
};

static const struct client_setting client_settings[] = {
    {"client.graphics.display_mode", "0"},
    {"client.graphics.width", "1920"},
    {"client.graphics.height", "1080"},
    {"client.graphics.maximized", "false"},
    {"client.gui.swcursor", "false"},
};

static char input_buffer[CONFIG_CAPACITY];
static char output_buffer[CONFIG_CAPACITY];

static int append_bytes(size_t *output_size, const void *data, size_t size)
{
    if (size > sizeof(output_buffer) - *output_size) return -1;
    memcpy(output_buffer + *output_size, data, size);
    *output_size += size;
    return 0;
}

static int append_setting(size_t *output_size, const struct client_setting *setting)
{
    const size_t key_size = strlen(setting->key), value_size = strlen(setting->value);
    return append_bytes(output_size, setting->key, key_size) ||
           append_bytes(output_size, "=", 1) ||
           append_bytes(output_size, setting->value, value_size) ||
           append_bytes(output_size, "\n", 1) ? -1 : 0;
}

int pokemmops5_configure_client(const char *path)
{
    unsigned seen = 0;
    size_t input_size = 0, output_size = 0, at = 0;
    FILE *file = fopen(path, "rb");

    if (file)
    {
        input_size = fread(input_buffer, 1, sizeof(input_buffer), file);
        int failed = ferror(file);
        if (input_size == sizeof(input_buffer) && fgetc(file) != EOF) failed = 1;
        if (fclose(file)) failed = 1;
        if (failed) return -1;
    }
    else if (errno != ENOENT) return -1;

    while (at < input_size)
    {
        const char *line = input_buffer + at;
        const char *newline = memchr(line, '\n', input_size - at);
        const size_t line_size = newline ? (size_t)(newline + 1 - line) : input_size - at;
        size_t content_size = newline ? line_size - 1 : line_size;
        if (content_size && line[content_size - 1] == '\r') content_size--;
        const char *equals = memchr(line, '=', content_size);
        int matched = 0;

        for (unsigned i = 0; i < sizeof(client_settings) / sizeof(client_settings[0]); i++)
        {
            const size_t key_size = strlen(client_settings[i].key);
            if (equals && (size_t)(equals - line) == key_size && !memcmp(line, client_settings[i].key, key_size))
            {
                if (append_setting(&output_size, &client_settings[i])) return -1;
                seen |= 1u << i;
                matched = 1;
                break;
            }
        }
        if (!matched && append_bytes(&output_size, line, line_size)) return -1;
        at += line_size;
    }

    for (unsigned i = 0; i < sizeof(client_settings) / sizeof(client_settings[0]); i++)
    {
        if (seen & (1u << i)) continue;
        if (output_size && output_buffer[output_size - 1] != '\n' && append_bytes(&output_size, "\n", 1)) return -1;
        if (append_setting(&output_size, &client_settings[i])) return -1;
    }

    if (input_size == output_size && !memcmp(input_buffer, output_buffer, input_size)) return 0;

    char temporary[4096];
    if (snprintf(temporary, sizeof(temporary), "%s.ps5.tmp", path) >= (int)sizeof(temporary)) return -1;
    file = fopen(temporary, "wb");
    if (!file) return -1;
    int failed = fwrite(output_buffer, 1, output_size, file) != output_size;
    if (fflush(file)) failed = 1;
    if (fclose(file)) failed = 1;
    if (failed || rename(temporary, path))
    {
        remove(temporary);
        return -1;
    }
    return 1;
}
