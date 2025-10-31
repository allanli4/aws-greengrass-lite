#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("Starting Failing Component...\n");
    
    // This will cause the component to fail
    FILE *fp = fopen("/nonexistent/path/file.txt", "r");
    if (!fp) {
        fprintf(stderr, "FATAL ERROR: Cannot open required file!\n");
        exit(1);
    }
    
    return 0;
}