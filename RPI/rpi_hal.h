#ifndef RPI_HAL_H
#define RPI_HAL_H

#include "shared_types.h"

// Struct to hold data for curl's WriteMemoryCallback
struct MemoryStruct {
  char *memory;
  size_t size;
};

/**
 * @file rpi_hal.h
 * @brief Hardware Abstraction Layer for RPi Robot Controller.
 *
 * Declares functions for interacting with hardware components and network services.
 * This version is more abstract and uses the shared data types.
 */

// --- Initialization ---
int init_serial_port(const char* device, int baud_rate);

// --- Android Communication ---
// Unified function to send JSON to Android. If is_object is true, value is treated as a raw JSON object; otherwise it's quoted as a string.
int send_android_json(int fd, const char* cat, const char* value, bool is_object);

// Wrapper: Sends {"cat": "status", "value": "status_message"}
int send_android_ack(int fd, const char* status_message);

// Wrapper: Sends {"cat": "image-rec", "value": {...}}
int send_image_recognition_to_android(int fd, int x, int y, const char* d, int image_id, int obstacle_id);

// Low-level function for sending messages with retries
int send_message_to_android_with_ack(int fd, const char* message);

// New function to parse the full Android JSON, including obstacles with 'd' and robot start position
int parse_android_map_and_obstacles(const char* json_string, SharedAppContext* context);

// New function to parse and execute direct STM commands from Android
int parse_and_execute_android_command(int stm32_fd, const char* android_command_str, SharedAppContext* context);

// --- PC/Server Communication ---
int post_data_to_server(const char* url, const char* payload, char* response_buffer, int buffer_size);
// Modified to pass SharedAppContext to store snap_positions and robot initial position
int parse_command_route_from_server(const char* json_string, Command commands[], int* command_count, SnapPosition snap_positions[], int* snap_position_count);

// --- STM32 Communication ---
uint32_t send_command_to_stm32(int fd, Command command, uint32_t external_cmd_id);

// --- Camera/Image Processing ---
int capture_image(const char* filename);

int get_img_id_from_class_name(const char* class_name);

// --- Helper functions ---
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);


#endif // RPI_HAL_H
