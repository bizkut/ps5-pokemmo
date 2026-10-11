/* Host behavior test for PokeMMO's launch-time graphics preferences. MIT. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../app/runtime/pokemmo_config.c"

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    assert(!fclose(file));
}

static size_t read_file(const char *path, char *data, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(data, 1, capacity, file);
    assert(!ferror(file));
    assert(feof(file));
    assert(!fclose(file));
    return size;
}

int main(void)
{
    static const char initial[] =
        "# retain this comment\n"
        "client.graphics.width=1024\n"
        "client.graphics.height=768\n"
        "client.graphics.maximized=true\n"
        "client.graphics.display_mode=2\n"
        "client.gui.swcursor=true\n"
        "client.audio.master=0.75\n";
    static const char expected[] =
        "# retain this comment\n"
        "client.graphics.width=1920\n"
        "client.graphics.height=1080\n"
        "client.graphics.maximized=false\n"
        "client.graphics.display_mode=0\n"
        "client.gui.swcursor=false\n"
        "client.audio.master=0.75\n";
    static const char defaults[] =
        "client.graphics.display_mode=0\n"
        "client.graphics.width=1920\n"
        "client.graphics.height=1080\n"
        "client.graphics.maximized=false\n"
        "client.gui.swcursor=false\n";
    char directory[] = "/tmp/pokemmo-config-test.XXXXXX";
    char path[256], actual[131072];
    assert(mkdtemp(directory));
    assert(snprintf(path, sizeof(path), "%s/main.properties", directory) < (int)sizeof(path));

    write_file(path, initial, sizeof(initial) - 1);
    assert(pokemmops5_configure_client(path) == 1);
    size_t size = read_file(path, actual, sizeof(actual));
    assert(size == sizeof(expected) - 1);
    assert(!memcmp(actual, expected, size));

    assert(pokemmops5_configure_client(path) == 0);
    size = read_file(path, actual, sizeof(actual));
    assert(size == sizeof(expected) - 1);
    assert(!memcmp(actual, expected, size));

    assert(unlink(path) == 0);
    assert(pokemmops5_configure_client(path) == 1);
    size = read_file(path, actual, sizeof(actual));
    assert(size == sizeof(defaults) - 1);
    assert(!memcmp(actual, defaults, size));

    memset(actual, 'x', 65537);
    write_file(path, actual, 65537);
    assert(pokemmops5_configure_client(path) == -1);
    size = read_file(path, actual, sizeof(actual));
    assert(size == 65537);
    for (size_t i = 0; i < size; i++) assert(actual[i] == 'x');

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    puts("PokeMMO config test PASS");
    return 0;
}
