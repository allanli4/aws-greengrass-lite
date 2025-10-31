//! System Monitor Component - Publishes system telemetry to IoT Core using GGL SDK

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
static float get_cpu_usage(void);
static float get_memory_usage(void);
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
    
    char response[2048];
    char client_token[64] = {0};
    // char shell[64] = "/bin/bash";
    char script[512] = {0};
    // int timeout_seconds = 10;
    
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
        snprintf(response, sizeof(response),
            "{\"clientToken\":\"%s\",\"stdout\":\"\",\"stderr\":\"No script provided\",\"exitCode\":1}",
            client_token);
    } else {
        // Execute the script
        FILE *fp = popen(script, "r");
        if (fp) {
            char stdout_output[1024] = {0};
            size_t len = fread(stdout_output, 1, sizeof(stdout_output)-1, fp);
            int exit_code = pclose(fp);
            
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

// Read CPU usage from /proc/stat
static float get_cpu_usage(void) {
    static unsigned long prev_idle = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1.0;
    
    unsigned long user, nice, system, idle, iowait, irq, softirq;
    if (fscanf(fp, "cpu %lu %lu %lu %lu %lu %lu %lu", 
               &user, &nice, &system, &idle, &iowait, &irq, &softirq) != 7) {
        fclose(fp);
        return -1.0;
    }
    fclose(fp);
    
    unsigned long total = user + nice + system + idle + iowait + irq + softirq;
    unsigned long diff_idle = idle - prev_idle;
    unsigned long diff_total = total - prev_total;
    
    float cpu_percent = (diff_total == 0) ? 0.0 : (diff_total - diff_idle) * 100.0 / diff_total;
    
    prev_idle = idle;
    prev_total = total;
    
    return cpu_percent;
}

// Read memory usage from /proc/meminfo
static float get_memory_usage(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1.0;
    
    unsigned long mem_total = 0, mem_available = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) break;
    }
    fclose(fp);
    
    if (mem_total == 0) return -1.0;
    return ((float)(mem_total - mem_available) / mem_total) * 100.0;
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
    
    printf("Starting System Monitor Component (GGL SDK)...\n");
    
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
    
    // Main monitoring loop
    while (true) {
        // Process any pending commands
        process_pending_command();
        
        float cpu_usage = get_cpu_usage();
        float memory_usage = get_memory_usage();
        time_t now = time(NULL);
        
        // Create JSON telemetry message
        char telemetry[512];
        snprintf(telemetry, sizeof(telemetry),
            "{"
            "\"timestamp\":%ld,"
            "\"cpu_percent\":%.2f,"
            "\"memory_percent\":%.2f,"
            "\"device_id\":\"gglite-monitor-c\""
            "}",
            now, cpu_usage, memory_usage
        );
        
        // Publish to IoT Core using GGL SDK
        ret = ggipc_publish_to_iot_core(
            GGL_STR("device/telemetry"), 
            ggl_buffer_from_null_term(telemetry), 
            0
        );
        
        if (ret != GGL_ERR_OK) {
            fprintf(stderr, "Failed to publish telemetry.\n");
        } else {
            printf("Published: %s\n", telemetry);
        }
        
        sleep(30); // Publish every 30 seconds
    }
    
    return 0;
}