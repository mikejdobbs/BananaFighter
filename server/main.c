#include "embed.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void apply_session_password(int argc, char **argv)
{
    const char *key = getenv("BANANAFIGHTER_KEY");
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-k"))
            key = argv[i + 1];
    if (key && key[0])
        server_set_session_password(key);
}

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-v") == 0 || strcmp(argv[a], "--version") == 0) {
            printf("BananaFighter server %s\n", BF_VERSION);
            return 0;
        }
    }
    if (crypto_session_init() != 0)
        return 1;
    apply_session_password(argc, argv);
    if (server_run_forever() < 0)
        return 1;
    return 0;
}
