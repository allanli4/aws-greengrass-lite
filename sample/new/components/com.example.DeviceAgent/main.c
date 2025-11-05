//! Device Agent Component - Executes remote commands via IoT Core using GGL SDK

#define _GNU_SOURCE
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/ipc/client.h>
#include <ggl/sdk.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

// Forward declarations
static char* get_device_id(void);

// Command queue for deferred processing
static char pending_command[256] = {0};
static bool has_pending_command = false;
static char device_id[64] = {0};

// Command handler for incoming IoT Core messages
static void command_handler(
    void *ctx,
    GglBuffer topic,
    GglBuffer payload,
    GgIpcSubscriptionHandle handle
) {
    (void) ctx;
    (void) handle;
    
    printf("Received command on [%.*s]: %.*s\n",
        (int) topic.len, topic.data,
        (int) payload.len, payload.data
    );
    
    // Queue command for processing outside callback
    if (!has_pending_command && payload.len < sizeof(pending_command)) {
        memcpy(pending_command, payload.data, payload.len);
        pending_command[payload.len] = '\0';
        has_pending_command = true;
    }
}

// Process queued commands outside callback
static void process_pending_command(void) {
    if (!has_pending_command) return;
    
    char response[96*1024];  // ~96KB for response (leaves room for base64 overhead)
    char client_token[64] = {0};
    char script[512] = {0};
    
    // Parse JSON command
    char *token_start = strstr(pending_command, "\"clientToken\":");
    if (token_start) {
        token_start = strchr(token_start + 14, '"');
        if (token_start) {
            token_start++;
            char *token_end = strchr(token_start, '"');
            if (token_end) {
                size_t len = token_end - token_start;
                if (len < sizeof(client_token)) {
                    strncpy(client_token, token_start, len);
                }
            }
        }
    }
    
    char *script_start = strstr(pending_command, "\"script\":");
    if (script_start) {
        script_start = strchr(script_start + 9, '"');
        if (script_start) {
            script_start++;
            char *script_end = strchr(script_start, '"');
            if (script_end) {
                size_t len = script_end - script_start;
                if (len < sizeof(script)) {
                    strncpy(script, script_start, len);
                }
            }
        }
    }
    
    if (strlen(script) == 0) {
        char examples[4096] = "See examples.txt file for command formats";
        FILE *examples_file = fopen("/var/lib/greengrass/packages/artifacts/com.example.DeviceAgent/1.0.1/examples.txt", "r");
        if (examples_file) {
            size_t read_len = fread(examples, 1, sizeof(examples)-1, examples_file);
            examples[read_len] = '\0';
            fclose(examples_file);
            // Replace newlines with spaces for JSON
            for (size_t i = 0; i < read_len; i++) {
                if (examples[i] == '\n') examples[i] = ' ';
                if (examples[i] == '"') examples[i] = '\'';
            }
        }
        snprintf(response, sizeof(response),
            "{\"clientToken\":\"%s\",\"stdout\":\"%s\",\"stderr\":\"No script provided\",\"exitCode\":1}",
            client_token, examples);
    } else {
        // Execute the script
        FILE *fp = popen(script, "r");
        if (fp) {
            char stdout_output[90*1024] = {0};  // ~90KB for command output
            size_t len = fread(stdout_output, 1, sizeof(stdout_output)-1, fp);
            int exit_code = pclose(fp);
            
            if (len == 0) {
                FILE *examples_file = fopen("/var/lib/greengrass/packages/artifacts/com.example.DeviceAgent/1.0.1/examples.txt", "r");
                if (examples_file) {
                    len = fread(stdout_output, 1, sizeof(stdout_output)-1, examples_file);
                    stdout_output[len] = '\0';
                    fclose(examples_file);
                } else {
                    strcpy(stdout_output, "Command returned no output. Examples file not found.");
                    len = strlen(stdout_output);
                }
            }
            
            // Escape quotes and newlines for JSON
            for (size_t i = 0; i < len; i++) {
                if (stdout_output[i] == '"') stdout_output[i] = '\'';
                if (stdout_output[i] == '\n') stdout_output[i] = ' ';
            }
            
            snprintf(response, sizeof(response),
                "{\"clientToken\":\"%s\",\"stdout\":\"%s\",\"stderr\":\"\",\"exitCode\":%d}",
                client_token, stdout_output, WEXITSTATUS(exit_code));
        } else {
            snprintf(response, sizeof(response),
                "{\"clientToken\":\"%s\",\"stdout\":\"\",\"stderr\":\"Failed to execute script\",\"exitCode\":1}",
                client_token);
        }
    }
    
    char response_topic[128];
    snprintf(response_topic, sizeof(response_topic), "greengrass/device-agent/%s/logs", device_id);
    
    GglError ret = ggipc_publish_to_iot_core(
        ggl_buffer_from_null_term(response_topic), 
        ggl_buffer_from_null_term(response),
        0
    );
    
    if (ret == GGL_ERR_OK) {
        printf("Sent response: %s\n", response);
    } else {
        fprintf(stderr, "Failed to send command response\n");
    }
    
    has_pending_command = false;
}

// Get device ID from config.yaml
static char* get_device_id(void) {
    static char thing_name[64] = {0};
    if (strlen(thing_name) > 0) return thing_name;
    
    FILE *fp = fopen("/etc/greengrass/config.yaml", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "thingName:")) {
                char *start = strchr(line, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        size_t len = end - start;
                        if (len < sizeof(thing_name)) {
                            strncpy(thing_name, start, len);
                            thing_name[len] = '\0';
                        }
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    
    if (strlen(thing_name) == 0) {
        strcpy(thing_name, "unknown-device");
    }
    
    printf("Using thing name from config: %s\n", thing_name);
    return thing_name;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    
    printf("Starting Device Agent Component (GGL SDK)...\n");
    
    // Initialize GGL SDK
    ggl_sdk_init();
    
    // Connect to Greengrass IPC
    GglError ret = ggipc_connect();
    if (ret != GGL_ERR_OK) {
        fprintf(stderr, "Failed to connect to Greengrass nucleus.\n");
        exit(1);
    }
    printf("Connected to Greengrass Lite.\n");
    
    // Get device ID
    strcpy(device_id, get_device_id());
    printf("Device ID: %s\n", device_id);
    
    // Subscribe to device agent commands via IoT Core
    char command_topic[128];
    snprintf(command_topic, sizeof(command_topic), "greengrass/device-agent/%s/commands", device_id);
    
    ret = ggipc_subscribe_to_iot_core(
        ggl_buffer_from_null_term(command_topic), 
        0,
        &command_handler, 
        NULL, 
        NULL
    );
    if (ret != GGL_ERR_OK) {
        fprintf(stderr, "Failed to subscribe to device commands.\n");
        exit(1);
    }
    printf("Subscribed to device agent commands topic via IoT Core.\n");
    
    while (true) {
        // Process any pending commands
        process_pending_command();
    }
    
    return 0;
}