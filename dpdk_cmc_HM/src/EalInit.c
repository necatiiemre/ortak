#include "EalInit.h"
#include "Common.h"

#include <stdlib.h>
#include <string.h>

// Server port 2 — the only NIC CMC drives. Hard-coded here (rather than
// expecting the caller to pass --allow) so the binary works the same whether
// it's launched from the Makefile run target, by hand, or via MainSoftware.
#define CMC_PCI_ALLOWLIST "0000:41:00.0"

// Returns true if argv already contains an EAL allowlist flag (-a/--allow);
// in that case we don't second-guess the operator.
static bool eal_args_have_allowlist(int argc, char const *argv[])
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!a) continue;
        if (strcmp(a, "-a") == 0 || strcmp(a, "--allow") == 0) {
            return true;
        }
        // Long form with attached value: --allow=...
        if (strncmp(a, "--allow=", 8) == 0) {
            return true;
        }
    }
    return false;
}

int initialize_eal(int argc, char const *argv[])
{
    int return_value;

    printf("Initializing DPDK EAL...\n");

    // Inject -a 0000:41:00.0 (server port 2) when the caller hasn't already
    // provided an allowlist. Builds a fresh argv on the heap; rte_eal_init
    // does not retain the pointer past the call so it's safe to free after.
    char const **launch_argv = (char const **)argv;
    int launch_argc = argc;
    char **owned_argv = NULL;

    if (!eal_args_have_allowlist(argc, argv)) {
        owned_argv = (char **)calloc((size_t)argc + 2 + 1, sizeof(char *));
        if (owned_argv == NULL) {
            fprintf(stderr, "EAL allowlist injection: calloc failed\n");
            rte_exit(EXIT_FAILURE, "EAL init failed (alloc)\n");
        }
        owned_argv[0] = (char *)argv[0];
        owned_argv[1] = (char *)"-a";
        owned_argv[2] = (char *)CMC_PCI_ALLOWLIST;
        for (int i = 1; i < argc; i++) {
            owned_argv[i + 2] = (char *)argv[i];
        }
        owned_argv[argc + 2] = NULL;

        launch_argv = (char const **)owned_argv;
        launch_argc = argc + 2;

        printf("EAL: injected allowlist -a %s (server port 2)\n",
               CMC_PCI_ALLOWLIST);
    }

    return_value = rte_eal_init(launch_argc, (char **)launch_argv);
    if (return_value < 0)
    {
        free(owned_argv);
        fprintf(stderr, "Error with EAL initialization\n");
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }

    free(owned_argv);
    printf("EAL initialized successfully!\n");
    return return_value;
}

void print_eal_info(void)
{
    printf("\n=== DPDK EAL Information ===\n");
    printf("DPDK Version: %s\n", rte_version());
    printf("Main lcore ID: %u\n", rte_get_main_lcore());
    printf("Total available lcores: %u\n", rte_lcore_count());

    // Print socket to lcore mapping
    printf("Socket to Lcore mapping:\n");

    unsigned max_socket = 0;
    unsigned lcore_id;

    // Find max socket
    RTE_LCORE_FOREACH(lcore_id)
    {
        unsigned socket = rte_lcore_to_socket_id(lcore_id);
        if (socket > max_socket)
        {
            max_socket = socket;
        }
    }

    // Print lcores for each socket
    for (unsigned socket = 0; socket <= max_socket; socket++)
    {
        printf("  Socket %u -> [", socket);
        bool first = true;
        RTE_LCORE_FOREACH(lcore_id)
        {
            if (rte_lcore_to_socket_id(lcore_id) == socket)
            {
                if (!first)
                    printf(", ");
                printf("%u", lcore_id);
                first = false;
            }
        }
        printf("]\n");
    }
}

void print_lcore_info(void)
{
    unsigned lcore_id;

    printf("\nLcore Information:\n");
    printf("Enabled lcores: ");
    RTE_LCORE_FOREACH(lcore_id)
    {
        printf("%u ", lcore_id);
    }
    printf("\n");

    printf("Worker lcores: ");
    RTE_LCORE_FOREACH_WORKER(lcore_id)
    {
        printf("%u ", lcore_id);
    }
    printf("\n");
}

void printSocketToLcoreList()
{

    for (uint16_t socket = 0; socket < MAX_SOCKET; socket++)
    {
        printf("  Socket %u -> [", socket);
        for (uint16_t lcore_id = 0; lcore_id < MAX_LCORE; lcore_id++)
        {
            printf("%u ", socket_to_lcore[socket][lcore_id]);
        }
        printf("]\n");
    }

    for (uint16_t socket = 0; socket < MAX_SOCKET; socket++)
    {
        printf(" Unused  Socket %u -> [", socket);
        for (uint16_t lcore_id = 0; lcore_id < MAX_LCORE; lcore_id++)
        {
            printf("%u ", unused_socket_to_lcore[socket][lcore_id]);
        }
        printf("]\n");
    }
}

void cleanup_eal(void)
{
    printf("Cleaning up EAL resources...\n");
    rte_eal_cleanup();
}
