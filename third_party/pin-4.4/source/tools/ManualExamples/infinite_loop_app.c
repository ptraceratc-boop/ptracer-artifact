/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Test application that runs indefinitely in a loop, printing dots periodically.
 * When CTRL-C is received, it prompts for user confirmation before exiting.
 * 
 * This is useful for testing signal handling and process termination tools.
 */

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Global flag to indicate CTRL-C was received */
volatile sig_atomic_t ctrl_c_received = 0;

#ifdef _WIN32
/* Windows Console Control Handler for CTRL-C */
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
    {
        ctrl_c_received = 1;
        return TRUE; /* Indicate that we handled the signal */
    }
    return FALSE;
}
#else
/* Unix signal handler for SIGINT (CTRL-C) */
void sigint_handler(int sig)
{
    ctrl_c_received = 1;
}
#endif

int main(int argc, char* argv[])
{
    unsigned long long iteration = 0;
    const unsigned long long print_interval = 1000000000;
    char input[256];
    
    printf("Infinite Loop Application\n");
    printf("=========================\n");
    printf("Running indefinitely... Press CTRL-C to initiate shutdown.\n");
    printf("A dot will be printed every %llu iterations.\n\n", print_interval);
    fflush(stdout);
    
    /* Set up signal handler for CTRL-C */
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        fprintf(stderr, "Error: Unable to install console control handler\n");
        return 1;
    }
#else
    signal(SIGINT, sigint_handler);
#endif
    
    /* Infinite loop */
    while (1)
    {
        iteration++;
        
        /* Print a dot every print_interval iterations */
        if (iteration % print_interval == 0)
        {
            printf(".");
            fflush(stdout);
        }
        
        /* Check if CTRL-C was received */
        if (ctrl_c_received)
        {
            printf("\n\nCTRL-C received! The application is asking for confirmation.\n");
            printf("Do you want to exit? (yes/no): ");
            fflush(stdout);
            
            /* Wait for user input */
            if (fgets(input, sizeof(input), stdin) != NULL)
            {
                /* Remove newline if present */
                size_t len = strlen(input);
                if (len > 0 && input[len - 1] == '\n')
                {
                    input[len - 1] = '\0';
                }
                
                /* Check if user wants to exit */
                if (strcmp(input, "yes") == 0 || strcmp(input, "y") == 0 || strcmp(input, "Y") == 0)
                {
                    printf("Exiting application...\n");
                    exit(0);
                }
                else
                {
                    printf("Continuing execution... Press CTRL-C again to stop.\n\n");
                    fflush(stdout);
                    
                    /* Reset the flag and continue */
                    ctrl_c_received = 0;
                    
#ifndef _WIN32
                    /* Re-register signal handler (required on some Unix systems) */
                    signal(SIGINT, sigint_handler);
#endif
                    /* Windows console handler remains registered */
                }
            }
            else
            {
                /* EOF or error on input, exit */
                printf("\nInput error or EOF, exiting...\n");
                exit(1);
            }
        }
    }
    
    return 0;
}
