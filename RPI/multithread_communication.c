#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h> // For pthread_cond_timedwait
#include <errno.h> // For ETIMEDOUT
#include <ctype.h>

#include "shared_types.h"
#include "rpi_hal.h"
#include "json_parser.h" // New include

// Definition for DIR_MAP_ANDROID_STR, declared in shared_types.h
// Maps RPi internal (0,2,4,6) to Android's (1=N, 2=E, 3=S, 4=W)
const char* DIR_MAP_ANDROID_STR[8] = {
    "1", "12", "2", "23", "3", "34", "4", "41"
};

// --- Configuration Toggles ---
// Set to 1 to use Named Pipes (Simulation), 0 to use /dev/ devices (Hardware)
#ifndef STM32_SIM
#define STM32_SIM   0
#endif

#ifndef ANDROID_SIM
#define ANDROID_SIM 0
#endif

// Device Paths
const char* STM32_HW_DEVICE    = "/dev/ttyACM0";
const char* ANDROID_HW_DEVICE  = "/dev/rfcomm0";

const char* STM32_PIPE_WRITE   = "rpi_to_stm";
const char* STM32_PIPE_READ    = "stm_to_rpi";
const char* ANDROID_PIPE_READ  = "android_to_rpi";
const char* ANDROID_PIPE_WRITE = "rpi_to_android";

const char* PATHFINDING_SERVER_URL = "http://192.168.22.26:5000/path";
const char* IMAGE_SERVER_URL       = "http://192.168.22.21:5000/detect";

const int BAUD_RATE = 115200;
const char* CAPTURE_FILENAME = "capture.jpg";

// --- Global Shared Application Context ---
SharedAppContext g_app_context;

// Separate descriptor for reading ACKs in simulation mode
int g_stm32_ack_fd = -1;


// =================================================================================
// THREAD 3: Image Processing (Temporary, "Fire-and-Forget")
// =================================================================================
// Updated post_image_to_server_thread to return response for parsing
static int post_image_to_server_thread(int obstacle_id, char* response_buffer, int buffer_size) {
    CURL* curl;
    CURLcode res;
    int result = -1;

    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    if (chunk.memory == NULL) { // Check for malloc failure
        fprintf(stderr, "[ImgThread] Failed to allocate memory for CURL response.\n");
        return -1;
    }

    curl = curl_easy_init();
    if (curl) {
        printf("[ImgThread] Sending image for obstacle %d to image server at %s...\n", obstacle_id, IMAGE_SERVER_URL);
        curl_mime *form = curl_mime_init(curl);
        curl_mimepart *field;

        field = curl_mime_addpart(form); curl_mime_name(field, "image"); curl_mime_filedata(field, CAPTURE_FILENAME);
        char id_str[10]; snprintf(id_str, sizeof(id_str), "%d", obstacle_id);
        field = curl_mime_addpart(form); curl_mime_name(field, "object_id"); curl_mime_data(field, id_str, CURL_ZERO_TERMINATED);

        curl_easy_setopt(curl, CURLOPT_URL, IMAGE_SERVER_URL);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long code; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            if (code >= 200 && code < 300) {
                if (response_buffer != NULL && chunk.memory != NULL) { // Only copy if buffer provided and memory exists
                    strncpy(response_buffer, chunk.memory, buffer_size - 1);
                    response_buffer[buffer_size - 1] = '\0';
                }
                result = 0;
            } else {
                fprintf(stderr, "[ImgThread] Image server returned non-2xx response: %ld\n", code);
            }
        } else {
            fprintf(stderr, "[ImgThread] post_image_to_server_thread failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
        curl_mime_free(form);
    } else {
        fprintf(stderr, "[ImgThread] curl_easy_init() failed.\n");
    }
    free(chunk.memory); // Free after curl_easy_cleanup
    return result;
}

void* process_image_thread(void* args) {
    ImageTaskArgs* task_args = (ImageTaskArgs*)args;
    SharedAppContext* context = task_args->context;
    char image_server_response[2048]; // Buffer for image server JSON response
    char class_label[100]; // To hold the detected class label

    printf("[ImgThread] Capturing image for obstacle %d...\n", task_args->obstacle_id);
    if (capture_image(CAPTURE_FILENAME) != 0) {
        fprintf(stderr, "[ImgThread] Failed to capture image.\n");
        // Signal image capture failure by setting ID to 0 or another error code, or just don't signal
        pthread_mutex_lock(&context->image_capture_mutex);
        context->last_image_capture_id = 0; // Indicate failure or no successful capture
        pthread_cond_signal(&context->image_capture_cond);
        pthread_mutex_unlock(&context->image_capture_mutex);
    } else {
        printf("[ImgThread] Image captured successfully for obstacle %d.\n", task_args->obstacle_id);
        // Signal image capture success
        pthread_mutex_lock(&context->image_capture_mutex);
        context->last_image_capture_id = task_args->obstacle_id;
        pthread_cond_signal(&context->image_capture_cond);
        pthread_mutex_unlock(&context->image_capture_mutex);

        // Notify Android that capture is starting
        char capture_status[100];
        snprintf(capture_status, sizeof(capture_status), "Capturing image for obstacle %d", task_args->obstacle_id);
        send_android_json(context->android_write_fd, "image", capture_status, false);

        // Send image detection result to Android in the new JSON format
        if (post_image_to_server_thread(task_args->obstacle_id, image_server_response, sizeof(image_server_response)) == 0) {
            printf("[ImgThread] Image server response: %s\n", image_server_response);

            /* Compatible with object_detection_server.py: server returns success, detected, count, objects[] with class_label, img_id, confidence, bbox.
             * Use "count" for detection (integer); prefer "img_id" from JSON; do not skip Bullseye — use first object with valid img_id. */
            int count = 0;
            if (get_json_int(image_server_response, "count", &count) != 0 || count <= 0) {
                printf("[ImgThread] No object detected by image server for obstacle %d.\n", task_args->obstacle_id);
            } else {
                const char* objects_array_start = strstr(image_server_response, "\"objects\":[");
                if (objects_array_start) {
                    objects_array_start += strlen("\"objects\":[");
                    const char* ptr = objects_array_start;
                    int sent = 0;
                    while (*ptr && sent == 0) {
                        const char* obj_start = strchr(ptr, '{');
                        if (!obj_start) break;
                        int depth = 1;
                        const char* p = obj_start + 1;
                        while (*p && depth > 0) {
                            if (*p == '{') depth++;
                            else if (*p == '}') depth--;
                            p++;
                        }
                        if (depth != 0) break;
                        const char* obj_end = p - 1;
                        size_t obj_len = (size_t)(obj_end - obj_start + 1);
                        char single_obj_json[512];
                        if (obj_len >= sizeof(single_obj_json)) obj_len = sizeof(single_obj_json) - 1;
                        strncpy(single_obj_json, obj_start, obj_len);
                        single_obj_json[obj_len] = '\0';

                        if (get_json_string(single_obj_json, "class_label", class_label, sizeof(class_label)) != 0)
                            get_json_string(single_obj_json, "class", class_label, sizeof(class_label));
                        if (class_label[0] != '\0') {
                            /* Strip " - ..." suffix if present (server may send "Number 4 - 4") */
                            char* dash = strstr(class_label, " - ");
                            if (dash) *dash = '\0';
                            int img_id = -1;
                            if (get_json_int(single_obj_json, "img_id", &img_id) != 0 || img_id < 0)
                                img_id = get_img_id_from_class_name(class_label);
                            if (img_id >= 0) {
                                /* Send JSON format to Android: x, y, d, image-id, obstacle-id */
                                const char* dir_str = (task_args->robot_snap_position.d >= 0 && task_args->robot_snap_position.d < 8) ?
                                                       DIR_MAP_ANDROID_STR[task_args->robot_snap_position.d] : "U";
                                // Using cat "obstacle" to bypass Android's problematic getString check for "image-rec"
                                send_image_recognition_to_android(context->android_write_fd, 
                                                                  task_args->robot_snap_position.x, 
                                                                  task_args->robot_snap_position.y, 
                                                                  dir_str, 
                                                                  img_id, 
                                                                  task_args->obstacle_id);
                                printf("[ImgThread] Sent image detection result to Android: obstacle_id=%d, class_label=%s, img_id=%d\n", task_args->obstacle_id, class_label, img_id);
                                sent = 1;
                            } else {
                                fprintf(stderr, "[ImgThread] Unknown class label received or invalid img_id: %s\n", class_label);
                            }
                        }
                        ptr = p;
                    }
                    if (sent == 0)
                        fprintf(stderr, "[ImgThread] No valid object with img_id for obstacle %d.\n", task_args->obstacle_id);
                }
            }
        } else {
            fprintf(stderr, "[ImgThread] Failed to upload image or no ACK received from image server.\n");
            send_android_ack(context->android_write_fd, "Failed to capture image for obstacle");
        }
    }
    free(task_args); // Free the dynamically allocated arguments
    return NULL;
}


// =================================================================================
// THREAD 2: Navigation Executor (Main Logic)
// =================================================================================

void execute_navigation() {
    SharedAppContext* context = &g_app_context;
    printf("[NavThread] State: [NAVIGATING]. Executing %d commands.\n", context->command_count);

    // Tell Android we are starting
    send_android_ack(context->android_write_fd, "ready-to-roll");

    pthread_mutex_lock(&context->lock);
    context->snap_position_idx = 0; // Reset snap position index for new navigation
    pthread_mutex_unlock(&context->lock);

    uint32_t current_cmd_id = 1; // Start command IDs from 1 for the sequence

    for (int i = 0; i < context->command_count; i++) {
        pthread_mutex_lock(&context->lock);
        if (context->stop_requested) {
            printf("[NavThread] Stop requested. Aborting navigation.\n");
            context->stop_requested = false;
            context->state = STATE_IDLE;
            pthread_mutex_unlock(&context->lock);
            break;
        }
        pthread_mutex_unlock(&context->lock);

        Command cmd = context->commands[i];
        if (cmd.type == CMD_SNAPSHOT) {
            // ... (Snapshot logic remains the same)
            printf("[NavThread] --- Spawning image thread for obstacle %d ---\n", cmd.value);
            pthread_t tid;
            ImageTaskArgs* args = malloc(sizeof(ImageTaskArgs));
            if (!args) {
                fprintf(stderr, "[NavThread] Failed to allocate ImageTaskArgs.\n");
                continue;
            }
            args->context = context;
            args->obstacle_id = cmd.value;
            // Get current snap position from context
            pthread_mutex_lock(&context->lock);
            if (context->snap_position_idx < context->snap_position_count) {
                args->robot_snap_position = context->snap_positions[context->snap_position_idx];
                context->snap_position_idx++;
            } else {
                // Fallback if snap positions don't match commands, should not happen with correct parsing
                args->robot_snap_position = (SnapPosition){.x = -1, .y = -1, .d = -1};
                fprintf(stderr, "[NavThread] Warning: Snap position index out of bounds.\n");
            }
            pthread_mutex_unlock(&context->lock);


            pthread_create(&tid, NULL, process_image_thread, args);
            pthread_detach(tid); // Detach to allow thread to clean up its resources automatically

            printf("[NavThread] Spawning image thread for obstacle %d. Waiting for image capture confirmation...\n", cmd.value);

            struct timespec ts_img;
            clock_gettime(CLOCK_REALTIME, &ts_img);
            ts_img.tv_sec += 10; // Wait for up to 10 seconds for image capture confirmation

            int img_ack_result = 0; // 0 for success, -1 for error/timeout
            pthread_mutex_lock(&context->image_capture_mutex);
            while (context->last_image_capture_id != (uint32_t)cmd.value && !context->stop_requested) {
                int rc = pthread_cond_timedwait(&context->image_capture_cond, &context->image_capture_mutex, &ts_img);
                if (rc == ETIMEDOUT) {
                    fprintf(stderr, "[NavThread] Timeout waiting for image capture confirmation for obstacle %d.\n", cmd.value);
                    img_ack_result = -1; // Indicate error
                    break;
                } else if (rc != 0) {
                    fprintf(stderr, "[NavThread] Error waiting for image capture condition variable: %d\n", rc);
                    img_ack_result = -1; // Indicate error
                    break;
                }
            }

            if (img_ack_result == 0 && context->last_image_capture_id == (uint32_t)cmd.value) {
                printf("[NavThread] Received image capture confirmation for obstacle %d. Proceeding.\n", cmd.value);
            } else if (img_ack_result == 0 && context->last_image_capture_id == 0) {
                // This means an image capture failed (last_image_capture_id was set to 0)
                fprintf(stderr, "[NavThread] Image capture for obstacle %d indicated failure. Aborting navigation.\n", cmd.value);
                img_ack_result = -1; // Treat as failure for navigation flow
            }
            pthread_mutex_unlock(&context->image_capture_mutex);

            if (img_ack_result == -1 || context->stop_requested) {
                // If there was an error or stop was requested while waiting, break out of navigation
                pthread_mutex_lock(&context->lock);
                context->stop_requested = true; // Ensure stop state is propagated
                context->state = STATE_IDLE;
                pthread_mutex_unlock(&context->lock);
                break; // Exit the command execution loop
            }
        } else if (cmd.type == CMD_FINISH) {
            printf("[NavThread] Received FINISH command. Ending navigation.\n");
            break; // Exit the command execution loop
        } else {
            // Send command to STM32 with a sequential ID
            uint32_t sent_cmd_id = current_cmd_id++; // Store the ID we're sending
            send_command_to_stm32(context->stm32_fd, cmd, sent_cmd_id);
            printf("[NavThread] Sent command %u (Type: %d, Val: %d) to STM32. Waiting for ACK...\n", 
                   sent_cmd_id, cmd.type, cmd.value);

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; // Wait for up to 10 seconds for ACK

            int ack_result = 0; // 0 for success, -1 for error/timeout
            pthread_mutex_lock(&context->stm32_ack_mutex);
            while (context->stm32_last_ack_id != sent_cmd_id && !context->stop_requested) {
                int rc = pthread_cond_timedwait(&context->stm32_ack_cond, &context->stm32_ack_mutex, &ts);
                if (rc == ETIMEDOUT) {
                    fprintf(stderr, "[NavThread] Timeout waiting for ACK for command %u.\n", sent_cmd_id);
                    ack_result = -1; // Indicate error
                    break;
                } else if (rc != 0) {
                    fprintf(stderr, "[NavThread] Error waiting for ACK condition variable: %d\n", rc);
                    ack_result = -1; // Indicate error
                    break;
                }
            }

            if (ack_result == 0 && context->stm32_last_ack_id == sent_cmd_id) {
                printf("[NavThread] Received ACK for command %u.\n", sent_cmd_id);
                
                // Notify Android that the command is complete so the car moves on the UI
                char status_msg[100];
                const char* type_str = "FW";
                if (cmd.type == CMD_MOVE_BACKWARD) type_str = "BW";
                else if (cmd.type == CMD_TURN_LEFT) type_str = "FL";
                else if (cmd.type == CMD_TURN_RIGHT) type_str = "FR";
                
                snprintf(status_msg, sizeof(status_msg), "STM completed %s%d", type_str, cmd.value);
                send_android_ack(context->android_write_fd, status_msg);
            }
            pthread_mutex_unlock(&context->stm32_ack_mutex);

            if (ack_result == -1 || context->stop_requested) {
                // If there was an error or stop was requested while waiting, break out of navigation
                pthread_mutex_lock(&context->lock);
                context->stop_requested = true; // Ensure stop state is propagated
                context->state = STATE_IDLE;
                pthread_mutex_unlock(&context->lock);
                break; // Exit the command execution loop
            }
        }
    } // End of for loop
    // Using send_android_ack for navigation completion status
    send_android_ack(context->android_write_fd, "all-images-scan");
}

void* navigation_executor_thread(void* args) {
    SharedAppContext* context = (SharedAppContext*)args;

    while (1) {
        pthread_mutex_lock(&context->lock);
        while (!context->new_map_received && !context->stop_requested) {
            printf("[NavThread] State: [IDLE]. Waiting for new mission...\n");
            pthread_cond_wait(&context->new_task_cond, &context->lock);
        }
        printf("[NavThread] Woke up! new_map_received=%d, stop_requested=%d\n", 
               context->new_map_received, context->stop_requested);

        if (context->stop_requested) {
            context->state = STATE_IDLE;
            context->stop_requested = false;
        }

        if (context->new_map_received) {
            context->state = STATE_PATHFINDING;
            context->new_map_received = false;
        }
        pthread_mutex_unlock(&context->lock);

        if (context->state == STATE_PATHFINDING) {
            printf("[NavThread] State: [PATHFINDING]. Requesting route from server...\n");
            char payload[2048];
            char obstacles_str[1500] = ""; // To build the obstacles array string

            for (int i = 0; i < context->obstacle_count; i++) {
                char obs_item[100]; // Buffer for a single obstacle JSON object
                // Obstacle x, y are 0-indexed internally, server expects 0-indexed
                // Direction 'd' is integer, server expects integer
                snprintf(obs_item, sizeof(obs_item), "{\"id\":%d,\"x\":%d,\"y\":%d,\"d\":%d}",
                         context->obstacles[i].id, context->obstacles[i].x, context->obstacles[i].y, context->obstacles[i].d);
                strcat(obstacles_str, obs_item);
                if (i < context->obstacle_count - 1) strcat(obstacles_str, ",");
            }

            // Construct the full payload including robot initial state and retrying flag
            snprintf(payload, sizeof(payload), "{\"obstacles\":[%s],\"robot_x\":%d,\"robot_y\":%d,\"robot_dir\":%d,\"retrying\":false}",
                     obstacles_str, context->robot_start_x, context->robot_start_y, context->robot_start_dir);
            printf("[NavThread] Forwarding arena data to pathfinding server: %s\n", payload);

            char response[4096]; // Increased response buffer size
            if (post_data_to_server(PATHFINDING_SERVER_URL, payload, response, sizeof(response)) == 0) {
                // --- DEBUG: Print raw server response ---
                printf("[NavThread] Raw server response:\n---\n%s\n---\n", response);

                // Call the modified parse_command_route_from_server
                if (parse_command_route_from_server(response, context->commands, &context->command_count,
                                                    context->snap_positions, &context->snap_position_count) == 0) {
                    send_android_ack(context->android_write_fd, "Route calculated. Navigating."); // Using ack send
                    execute_navigation();
                } else {
                    send_android_ack(context->android_write_fd, "Error: Pathfinding failed to parse route."); // Using ack send
                }
            } else {
                send_android_ack(context->android_write_fd, "Error: Pathfinding server communication failed."); // Using ack send
            }
        }

        pthread_mutex_lock(&context->lock);
        context->state = STATE_IDLE;
        pthread_mutex_unlock(&context->lock);
    }
    return NULL;
}
            
            
            // =================================================================================
            // THREAD 1: Android Listener (High-level Commands)
            // =================================================================================
            
            void* android_listener_thread(void* args) {
                SharedAppContext* context = (SharedAppContext*)args;
                char buffer[8192];      // Temporary read buffer
                char main_buffer[16384]; // Persistent accumulation buffer
                int buffer_pos = 0;
            
                while (1) {
                    printf("[AndroidThread] Listening for messages...\n");
                    ssize_t bytes_read = read(context->android_fd, buffer, sizeof(buffer) - 1);
            
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("[AndroidThread] Received raw: %s\n", buffer);

                        // Append new data to main buffer
                        if (buffer_pos + bytes_read >= (ssize_t)sizeof(main_buffer)) {
                            fprintf(stderr, "[AndroidThread] Buffer overflow. Clearing buffer.\n");
                            buffer_pos = 0;
                        }
                        memcpy(main_buffer + buffer_pos, buffer, bytes_read);
                        buffer_pos += bytes_read;
                        main_buffer[buffer_pos] = '\0';
            
                        // Process all complete JSON messages (by matching braces)
                        char* msg_start;
                        while ((msg_start = strchr(main_buffer, '{')) != NULL) {
                            int depth = 0;
                            char* p = msg_start;
                            char* msg_end = NULL;
                            bool in_string = false;

                            while (*p) {
                                if (*p == '"' && (p == msg_start || *(p-1) != '\\')) {
                                    in_string = !in_string;
                                } else if (!in_string) {
                                    if (*p == '{') depth++;
                                    else if (*p == '}') {
                                        depth--;
                                        if (depth == 0) {
                                            msg_end = p;
                                            break;
                                        }
                                    }
                                }
                                p++;
                            }

                            if (msg_end == NULL) {
                                // Incomplete message, wait for more data
                                break;
                            }

                            // We have a complete message from msg_start to msg_end
                            size_t msg_len = msg_end - msg_start + 1;
                            char current_msg[8192]; 
                            if (msg_len >= sizeof(current_msg)) msg_len = sizeof(current_msg) - 1;
                            memcpy(current_msg, msg_start, msg_len);
                            current_msg[msg_len] = '\0';

                            printf("[AndroidThread] Found complete JSON message, processing: %s\n", current_msg);
            
                                // Check for JSON message first
                                char category[50];
                                if (get_json_string(current_msg, "cat", category, sizeof(category)) == 0) {
                                    if (strcmp(category, "sendArena") == 0) {
                                        const char* value_ptr = strstr(current_msg, "\"value\":");
                                        if (value_ptr) {
                                            const char* map_json_start = strchr(value_ptr, '{');
                                            if (map_json_start) {
                                                pthread_mutex_lock(&context->lock);
                                                if (context->state == STATE_IDLE) {
                                                    if (parse_android_map_and_obstacles(map_json_start, context) == 0) {
                                                        context->new_map_received = true;
                                                        send_android_ack(context->android_write_fd, "Map received. Pathfinding...");
                                                        pthread_cond_signal(&context->new_task_cond);
                                                    } else {
                                                        send_android_ack(context->android_write_fd, "Error: Invalid map format.");
                                                    }
                                                } else {
                                                    send_android_ack(context->android_write_fd, "Error: Robot is busy. Cannot start new mission.");
                                                }
                                                pthread_mutex_unlock(&context->lock);
                                            } else {
                                                fprintf(stderr, "[AndroidThread] Malformed 'sendArena': 'value' object not found.\n");
                                                send_android_ack(context->android_write_fd, "Error: Malformed 'sendArena' message.");
                                            }
                                        } else {
                                            fprintf(stderr, "[AndroidThread] Malformed 'sendArena': 'value' key not found.\n");
                                            send_android_ack(context->android_write_fd, "Error: Malformed 'sendArena' message.");
                                        }
                                    } else if (strcmp(category, "stop") == 0) { // STOP command as JSON
                                        pthread_mutex_lock(&context->lock);
                                        send_android_ack(context->android_write_fd, "STOP command received.");
                                        context->stop_requested = true;
                                        if(context->state != STATE_IDLE) {
                                            pthread_cond_signal(&context->new_task_cond);
                                        }
                                        pthread_mutex_unlock(&context->lock);
                                    } else if (strcmp(category, "stm") == 0) { // Direct STM command from Android
                                        char stm_command_str[100]; // Buffer for the command string like "<FR090>"
                                        if (get_json_string(current_msg, "value", stm_command_str, sizeof(stm_command_str)) == 0) {
                                            parse_and_execute_android_command(context->stm32_fd, stm_command_str, context);
                                        } else {
                                            fprintf(stderr, "[AndroidThread] Malformed 'stm' command: 'value' key not found.\n");
                                            send_android_ack(context->android_write_fd, "Error: Malformed STM command.");
                                        }
                                    } else {
                                        fprintf(stderr, "[AndroidThread] Unrecognized JSON category: %s\n", category);
                                    }
                                } else {
                                    fprintf(stderr, "[AndroidThread] Malformed or unrecognized message: %s\n", current_msg);
                                }

                            // Shift remaining data to the front
                            // Also consume any trailing whitespace/newlines after the JSON object
                            char* next_ptr = msg_end + 1;
                            while (*next_ptr && (isspace((unsigned char)*next_ptr) || *next_ptr == '\n' || *next_ptr == '\r')) {
                                next_ptr++;
                            }

                            int consumed = next_ptr - main_buffer;
                            int remaining = buffer_pos - consumed;
                            if (remaining > 0) {
                                memmove(main_buffer, next_ptr, remaining);
                                buffer_pos = remaining;
                                main_buffer[buffer_pos] = '\0';
                            } else {
                                buffer_pos = 0;
                                main_buffer[0] = '\0';
                            }
                        }
                    } else if (bytes_read == 0) {
                        usleep(10000);
                    } else {
                        perror("[AndroidThread] Error reading from serial port");
                        usleep(100000);
                    }
                }
                return NULL;
            }
            
            
            // =================================================================================
            // New THREAD: STM32 Listener
            // =================================================================================
            void* stm32_listener_thread(void* args) {
                SharedAppContext* context = (SharedAppContext*)args;
                char buffer[256];      // Temporary read buffer
                char main_buffer[512]; // Persistent accumulation buffer
                int buffer_pos = 0;
            
                printf("[STM32Thread] Listening for messages...\n");
            
                while (1) {
                    ssize_t bytes_read = read(g_stm32_ack_fd, buffer, sizeof(buffer) - 1);
            
                                if (bytes_read > 0) {
                                    buffer[bytes_read] = '\0';
                                    printf("[STM32Thread] Received raw: %s\n", buffer);
                    
                                    // Append new data to main buffer
                                    if (buffer_pos + bytes_read >= (ssize_t)sizeof(main_buffer)) {                            fprintf(stderr, "[STM32Thread] Buffer overflow. Clearing buffer.\n");
                            buffer_pos = 0;
                        }
                        memcpy(main_buffer + buffer_pos, buffer, bytes_read);
                        buffer_pos += bytes_read;
                        main_buffer[buffer_pos] = '\0';
            
                        // Process all complete messages (delimited by ;)
                        char* semicolon_pos;
                        while ((semicolon_pos = strchr(main_buffer, ';')) != NULL) {
                            size_t msg_len = (semicolon_pos - main_buffer) + 1;
                            char current_msg[256];
                            if (msg_len >= sizeof(current_msg)) msg_len = sizeof(current_msg) - 1;
                            
                            memcpy(current_msg, main_buffer, msg_len);
                            current_msg[msg_len] = '\0';
            
                            printf("[STM32Thread] Processing: %s\n", current_msg);
            
                            uint32_t cmd_id;
                            if (sscanf(current_msg, " !%u/DONE;", &cmd_id) == 1) {
                                pthread_mutex_lock(&context->stm32_ack_mutex);
                                context->stm32_last_ack_id = cmd_id;
                                pthread_cond_signal(&context->stm32_ack_cond);
                                pthread_mutex_unlock(&context->stm32_ack_mutex);
                                printf("[STM32Thread] Processed ACK for CMD ID: %u\n", cmd_id);
                            } else {
                                fprintf(stderr, "[STM32Thread] Unrecognized format: %s\n", current_msg);
                            }
            
                            // Shift remaining data to the front
                            int remaining = buffer_pos - msg_len;
                            if (remaining > 0) {
                                memmove(main_buffer, main_buffer + msg_len, remaining);
                                buffer_pos = remaining;
                                main_buffer[buffer_pos] = '\0';
                            } else {
                                buffer_pos = 0;
                                main_buffer[0] = '\0';
                            }
                        }
                    } else if (bytes_read == 0) {
                        usleep(10000);
                    } else {
                        perror("[STM32Thread] Error reading from serial port");
                        usleep(100000);
                    }
                }
                return NULL;
            }
            
            
            // =================================================================================
            // Main Function (Initialization and Thread Management)
            // =================================================================================

// =================================================================================
// Main Function (Initialization and Thread Management)
// =================================================================================

int main() {
    curl_global_init(CURL_GLOBAL_ALL); // Initialize curl once for the application lifecycle
    memset(&g_app_context, 0, sizeof(SharedAppContext));
    pthread_mutex_init(&g_app_context.lock, NULL);
    pthread_cond_init(&g_app_context.new_task_cond, NULL);
    g_app_context.state = STATE_IDLE;
    g_app_context.snap_position_count = 0; // Initialize new fields
    g_app_context.snap_position_idx = 0;   // Initialize new fields

    // Initialize STM32 ACK synchronization mechanisms
    g_app_context.stm32_last_ack_id = 0;
    pthread_mutex_init(&g_app_context.stm32_ack_mutex, NULL);
    pthread_cond_init(&g_app_context.stm32_ack_cond, NULL);

    // Initialize Image capture synchronization mechanisms
    g_app_context.last_image_capture_id = 0;
    pthread_mutex_init(&g_app_context.image_capture_mutex, NULL);
    pthread_cond_init(&g_app_context.image_capture_cond, NULL);


    // Initialize Android Communication
    #if ANDROID_SIM
        printf("Android: Simulation Mode (Pipes)\n");
        g_app_context.android_fd = init_serial_port(ANDROID_PIPE_READ, BAUD_RATE);
        g_app_context.android_write_fd = init_serial_port(ANDROID_PIPE_WRITE, BAUD_RATE);
    #else
        printf("Android: Hardware Mode (%s)\n", ANDROID_HW_DEVICE);
        g_app_context.android_fd = init_serial_port(ANDROID_HW_DEVICE, BAUD_RATE);
        g_app_context.android_write_fd = g_app_context.android_fd;
    #endif

    // Initialize STM32 Communication
    #if STM32_SIM
        printf("STM32:   Simulation Mode (Pipes)\n");
        g_app_context.stm32_fd = init_serial_port(STM32_PIPE_WRITE, BAUD_RATE);
        g_stm32_ack_fd = init_serial_port(STM32_PIPE_READ, BAUD_RATE);
    #else
        printf("STM32:   Hardware Mode (%s)\n", STM32_HW_DEVICE);
        g_app_context.stm32_fd = init_serial_port(STM32_HW_DEVICE, BAUD_RATE);
        g_stm32_ack_fd = g_app_context.stm32_fd;
    #endif

    if (g_app_context.stm32_fd == -1 || g_stm32_ack_fd == -1 || g_app_context.android_fd == -1 || g_app_context.android_write_fd == -1) {
        fprintf(stderr, "Fatal: Failed to initialize communication ports. Exiting.\n");
        return 1;
    }


    printf("--- RPi Control Centre Initialized ---\n");

    pthread_t android_tid, nav_tid, stm32_tid;
    pthread_create(&android_tid, NULL, android_listener_thread, &g_app_context);
    pthread_create(&nav_tid, NULL, navigation_executor_thread, &g_app_context);
    pthread_create(&stm32_tid, NULL, stm32_listener_thread, &g_app_context); // Create the new STM32 listener thread

    pthread_join(android_tid, NULL);
    pthread_join(nav_tid, NULL);
    pthread_join(stm32_tid, NULL); // Join the new STM32 listener thread

    pthread_mutex_destroy(&g_app_context.lock);
    pthread_cond_destroy(&g_app_context.new_task_cond);
    pthread_mutex_destroy(&g_app_context.stm32_ack_mutex); // Destroy new mutex
    pthread_cond_destroy(&g_app_context.stm32_ack_cond);   // Destroy new condition variable
    pthread_mutex_destroy(&g_app_context.image_capture_mutex); // Destroy image capture mutex
    pthread_cond_destroy(&g_app_context.image_capture_cond);   // Destroy image capture condition variable
    
    // Close file descriptors
    if (g_stm32_ack_fd != -1) close(g_stm32_ack_fd);
    if (g_app_context.stm32_fd != -1 && g_app_context.stm32_fd != g_stm32_ack_fd) {
        close(g_app_context.stm32_fd);
    }
    
    if (g_app_context.android_fd != -1) close(g_app_context.android_fd);
    if (g_app_context.android_write_fd != -1 && g_app_context.android_write_fd != g_app_context.android_fd) {
        close(g_app_context.android_write_fd);
    }


    curl_global_cleanup(); // Clean up curl once at application shutdown
    return 0;
}


// =================================================================================
// Testing Instructions (Simulation Mode)
// =================================================================================
/*
To test the RPi Control Centre without physical hardware (Android/STM32), follow these steps.
The simulation uses Named Pipes (FIFOs) for serial comms and the provided Python fake servers.

**Prerequisites:**
1.  Python 3 installed.
2.  libcurl development libraries (e.g., `sudo apt-get install libcurl4-openssl-dev`).
3.  Ensure all source files (multithread_communication.c, json_parser.c, rpi_hal.c) are present.

**Step 1: Compile the RPI communication module**
Use the provided Makefile to build the test configuration:

    make test_center

This creates an executable `./test_center` with STM32_SIM=1, ANDROID_SIM=1, and RPI_TESTING defined.

**Step 2: Create Named Pipes (FIFOs)**
Create the 4 pipes required for bidirectional simulation in the `RPI` directory:

    mkfifo rpi_to_stm stm_to_rpi android_to_rpi rpi_to_android

**Step 3: Run the Fake Servers and STM32 Simulator**
Open 3 separate terminals for the simulators:

*   Terminal 1 (Fake Pathfinding Server):
    python3 fake_path_server.py

*   Terminal 2 (Fake Image Recognition Server):
    python3 fake_image_server.py

*   Terminal 3 (Fake STM32 Simulation):
    python3 fake_stm.py

*Note: Ensure the URLs in `multithread_communication.c` (PATHFINDING_SERVER_URL, IMAGE_SERVER_URL) 
point to where these servers are running (e.g., "http://localhost:5000").*

**Step 4: Run the RPI Control Centre**
Open a 4th terminal and run the compiled program:

    ./test_center

**Step 5: Simulate Android Input**
Open a 5th terminal. Send a "sendArena" command as JSON to the `android_to_rpi` pipe.

Example Command (Start Mission):
    echo "{\"cat\": \"sendArena\", \"value\": {\"obstacles\":[{\"x\": 10,\"y\": 10,\"d\": 1,\"id\": 1},{\"x\": 5,\"y\": 15,\"d\": 2,\"id\": 2}],\"robot_x\": 2,\"robot_y\": 2,\"robot_dir\": 1}}" > android_to_rpi

Example Command (Direct STM control):
    echo "{\"cat\": \"stm\", \"value\": \"<FW10>\"}" > android_to_rpi

Example Command (Stop):
    echo "{\"cat\": \"stop\"}" > android_to_rpi

*Note: 
- Coordinates (x, y) are 1-indexed for Android; Directions: 1=N, 2=E, 3=S, 4=W.
- Category "stm" expects "<CMDVALUE>" format (e.g., <FW10>, <TL90>).*

**Step 6: Observe and Control**
*   **Monitor STM32 output:** Check Terminal 3 or run `cat rpi_to_stm`.
*   **Monitor Android feedback:** Run `cat rpi_to_android` in a separate terminal to see status Acks and Image-Rec results sent by the RPi.

**Cleanup:**
1.  Press Ctrl+C in all terminals.
2.  Remove pipes: `rm rpi_to_stm stm_to_rpi android_to_rpi rpi_to_android`
*/