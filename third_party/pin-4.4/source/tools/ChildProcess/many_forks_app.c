/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

void print_usage(const char* program_name) {
    printf("Usage: %s <num_iterations> [command_to_execute]\n", program_name);
    printf("  num_iterations: Number of child processes to create (1-1000)\n");
    printf("  command_to_execute: Optional command to run in child process\n");
    printf("                     If not provided, child will just print and exit\n");
    printf("\nNote: Using execv() - no shell features (pipes, redirection) supported\n");
}

int main(int argc, char** argv) {
    pid_t pid;
    int status;
    char *command = NULL;
   // Check arguments
    if (argc < 2 || argc > 3) 
    {
        print_usage(argv[0]);
        return 1;
    }    
    int num_processes = atoi(argv[1]);
    if (num_processes <= 0 || num_processes > 2000) 
    {
        fprintf(stderr, "Error: Number of iterations must be between 1 and 2000\n");
        print_usage(argv[0]);
        return 1;
    }   

    if (argc == 3) {
        // We have a command to execute from each forked process.
        command = argv[2];
    }     
    
    printf("Starting %d child processes serially...\n", num_processes);
    
    // Create child processes one at a time
    for (int i = 0; i < num_processes; i++) 
    {
        printf("Creating child process %d...\n", i + 1);
        fflush(stdout);  // Flush before fork
        pid = fork();
        
        if (pid == -1) 
        {
            // Fork failed
            perror("fork failed");
            exit(1);
        }
        else if (pid == 0) 
        {
            // Child process
            printf("  Child process %d (PID: %d) is running\n", i + 1, getpid());

            if (command) 
            {
                char* childArgvArray[2];
                childArgvArray[0] = argv[2];
                childArgvArray[1] = NULL;
                
                execv(childArgvArray[0], childArgvArray);
                
                // Shouldn't reach here
                printf("  Execve failed (%s) running\n",childArgvArray[0]);
                exit(1);  // Child exits successfully
            }
            else 
            {               
                printf("  Child process %d (PID: %d) is exiting\n", i + 1, getpid());
                exit(0);  // Child exits successfully
            }
            
        }
        else {
            // Parent process - wait for this specific child to complete
            printf("  Parent waiting for child %d (PID: %d)...\n", i + 1, pid);
            
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status)) {
                printf("  Child %d completed successfully (exit code: %d)\n", 
                       i + 1, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("  Child %d terminated by signal %d\n", i + 1, WTERMSIG(status));                        
            } else {
                printf("  Child %d terminated abnormally\n", i + 1);
            }
            
            printf("  Moving to next child...\n\n");
        }
    }
    
    printf("All %d child processes have completed serially!\n", num_processes);
    return 0;
}