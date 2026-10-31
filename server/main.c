#include "embed.h"

int main(void)
{
    if (server_run_forever() < 0)
        return 1;
    return 0;
}
