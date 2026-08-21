#include "json_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

#define JSON_MAX_FIELD_LEN 128
#define JSON_MAX_ARRAY_ELEMS 20

// Helper function to find a key and return a pointer to its value start
static const char* find_json_value(const char* json, const char* key) {
    char search_key[JSON_MAX_FIELD_LEN];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    const char* key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    
    const char* p = key_pos + strlen(search_key);
    // Skip whitespace after key
    while (*p && isspace((unsigned char)*p)) p++;
    // Must be a colon
    if (*p != ':') return NULL;
    p++;
    // Skip whitespace after colon
    while (*p && isspace((unsigned char)*p)) p++;
    
    return p;
}

// Function to extract an integer value from a JSON string for a given key
int get_json_int(const char *json_string, const char *key, int *value) {
    const char* val_start = find_json_value(json_string, key);
    if (!val_start) return -1;

    // Skip whitespace
    while (*val_start && isspace((unsigned char)*val_start)) {
        val_start++;
    }

    if (sscanf(val_start, "%d", value) == 1) {
        return 0; // Success
    }
    return -1; // Failure
}

// Function to extract a string value from a JSON string for a given key
int get_json_string(const char *json_string, const char *key, char *value_buffer, size_t buffer_size) {
    const char* val_start = find_json_value(json_string, key);
    if (!val_start) return -1;

    // Skip whitespace before the value
    while (*val_start && isspace((unsigned char)*val_start)) {
        val_start++;
    }

    if (*val_start != '"') return -1; // Check for opening quote

    val_start++; // Move past the opening quote
    const char* val_end = strchr(val_start, '"');
    if (!val_end) return -1;

    size_t len = val_end - val_start;
    if (len >= buffer_size) return -1; // Buffer too small

    strncpy(value_buffer, val_start, len);
    value_buffer[len] = '\0';
    return 0; // Success
}

// Helper for parsing a single obstacle object
static int parse_single_obstacle(const char* obs_str, Obstacle* obstacle) {
    int android_d = -1;
    int raw_x = -1, raw_y = -1;
    if (get_json_int(obs_str, "id", &obstacle->id) == 0 &&
        get_json_int(obs_str, "x", &raw_x) == 0 &&
        get_json_int(obs_str, "y", &raw_y) == 0 &&
        get_json_int(obs_str, "d", &android_d) == 0) {
        
        // Convert to 0-indexed
        obstacle->x = raw_x - 1;
        obstacle->y = raw_y - 1;

        // Map Android's 1=N, 2=E, 3=S, 4=W to RPi's 0,2,4,6
        switch (android_d) {
            case 1: obstacle->d = 0; break; // North
            case 2: obstacle->d = 2; break; // East
            case 3: obstacle->d = 4; break; // South
            case 4: obstacle->d = 6; break; // West
            default: obstacle->d = 0; break; // Default to North
        }
        return 0; // Success
    }
    return -1; // Failure
}

// Function to parse the Android map JSON into the SharedAppContext
int parse_android_map_json(const char* json_string, SharedAppContext* context) {
    const char* obstacles_array_start = strstr(json_string, "\"obstacles\":[");
    if (!obstacles_array_start) return -1;

    obstacles_array_start += strlen("\"obstacles\":[");
    const char* obstacles_array_end = strstr(obstacles_array_start, "]");
    if (!obstacles_array_end) return -1;

    char temp_obstacles_str[strlen(json_string) + 1];
    strncpy(temp_obstacles_str, obstacles_array_start, obstacles_array_end - obstacles_array_start);
    temp_obstacles_str[obstacles_array_end - obstacles_array_start] = '\0';

    context->obstacle_count = 0;
    const char* current_obs_str = temp_obstacles_str;
    while (current_obs_str && *current_obs_str != '\0' && context->obstacle_count < MAX_OBSTACLES) {
        const char* obj_start = strchr(current_obs_str, '{');
        if (!obj_start) break;
        const char* obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        char single_obs_json[JSON_MAX_FIELD_LEN];
        strncpy(single_obs_json, obj_start, obj_end - obj_start + 1);
        single_obs_json[obj_end - obj_start + 1] = '\0';

        if (parse_single_obstacle(single_obs_json, &context->obstacles[context->obstacle_count]) == 0) {
            context->obstacle_count++;
        } else {
            fprintf(stderr, "Error parsing single obstacle JSON: %s\n", single_obs_json);
        }
        current_obs_str = obj_end + 1;
        if (*current_obs_str == ',') current_obs_str++; // Skip comma if present
    }

    // Parse robot_x, robot_y, robot_direction
    int robot_x_val = 1; // Default to 1 (0-indexed)
    int robot_y_val = 1; // Default to 1 (0-indexed)
    int robot_dir_val = 0; // Default to 0 (North)

    if (get_json_int(json_string, "robot_x", &robot_x_val) != 0) {
        fprintf(stderr, "Could not parse robot_x, using default.\n");
    }
    if (get_json_int(json_string, "robot_y", &robot_y_val) != 0) {
        fprintf(stderr, "Could not parse robot_y, using default.\n");
    }

    int android_dir = 0;
    if (get_json_int(json_string, "robot_dir", &android_dir) == 0 || 
        get_json_int(json_string, "robot_direction", &android_dir) == 0) {
        // Map Android's 1=N, 2=E, 3=S, 4=W to RPi's 0,2,4,6
        switch (android_dir) {
            case 1: robot_dir_val = 0; break; // North
            case 2: robot_dir_val = 2; break; // East
            case 3: robot_dir_val = 4; break; // South
            case 4: robot_dir_val = 6; break; // West
            default: robot_dir_val = 0; break; // Default to North if unknown or invalid
        }
    } else {
        fprintf(stderr, "Could not parse robot_dir, using default.\n");
    }

    // Adjust coordinates from Android's 1-indexed to RPi's 0-indexed
    context->robot_start_x = robot_x_val - 1;
    context->robot_start_y = robot_y_val - 1;
    context->robot_start_dir = robot_dir_val;

    return 0; // Success
}

// Helper for parsing a single command string (e.g., "FW5000", "SP1")
static int parse_single_command_string(const char* cmd_str, Command* command) {
    if (strncmp(cmd_str, "FW", 2) == 0) {
        command->type = CMD_MOVE_FORWARD;
        command->value = atoi(cmd_str + 2);
    } else if (strncmp(cmd_str, "BW", 2) == 0) {
        command->type = CMD_MOVE_BACKWARD;
        command->value = atoi(cmd_str + 2);
    } else if (strncmp(cmd_str, "FL", 2) == 0) {
        command->type = CMD_TURN_LEFT;
        command->value = atoi(cmd_str + 2); // Assuming angle like 90
    } else if (strncmp(cmd_str, "FR", 2) == 0) {
        command->type = CMD_TURN_RIGHT;
        command->value = atoi(cmd_str + 2); // Assuming angle like 90
    } else if (strncmp(cmd_str, "SP", 2) == 0) {
        command->type = CMD_SNAPSHOT;
        command->value = atoi(cmd_str + 2); // Obstacle ID
    } else if (strncmp(cmd_str, "SNAP", 4) == 0) {
        command->type = CMD_SNAPSHOT;
        command->value = atoi(cmd_str + 4); // Obstacle ID
    } else if (strcmp(cmd_str, "FIN") == 0) {
        command->type = CMD_FINISH;
        command->value = 0;
    } else {
        fprintf(stderr, "Unknown command type: %s\n", cmd_str);
        return -1; // Unknown command
    }
    return 0;
}

// Function to parse the pathfinding server's route JSON
int parse_route_json(const char* json_string, Command commands[], int* command_count, SnapPosition snap_positions[], int* snap_position_count) {
    *command_count = 0;
    *snap_position_count = 0;
    int ret_val = -1; // Default to failure
    char* commands_substr = NULL;
    char* snaps_substr = NULL;

    // --- Find the "data" object ---
    const char* data_key_start = strstr(json_string, "\"data\""); // Find "data" key
    if (!data_key_start) {
        fprintf(stderr, "[Parser] 'data' key not found in server response.\n");
        return -1;
    }
    const char* data_obj_start = data_key_start + strlen("\"data\""); // Move past "data"
    while (*data_obj_start && isspace((unsigned char)*data_obj_start)) data_obj_start++; // Skip whitespace
    if (*data_obj_start != ':') { // Check for colon
        fprintf(stderr, "[Parser] Malformed JSON: missing ':' after 'data' key.\n");
        return -1;
    }
    data_obj_start++; // Move past ':'
    while (*data_obj_start && isspace((unsigned char)*data_obj_start)) data_obj_start++; // Skip whitespace
    if (*data_obj_start != '{') { // Check for opening brace
        fprintf(stderr, "[Parser] Malformed JSON: 'data' is not an object.\n");
        return -1;
    }
    data_obj_start++; // Move past '{' to inside the data object

    // --- Parse commands array ---
    const char* commands_key_start = strstr(data_obj_start, "\"commands\"");
    if (!commands_key_start) {
        fprintf(stderr, "[Parser] 'commands' array not found in server response.\n");
        return -1;
    }
    const char* commands_array_start = commands_key_start + strlen("\"commands\""); // Move past "commands"
    while (*commands_array_start && isspace((unsigned char)*commands_array_start)) commands_array_start++; // Skip whitespace
    if (*commands_array_start != ':') { // Check for colon
         fprintf(stderr, "[Parser] Malformed JSON: missing ':' after 'commands' key.\n");
         return -1;
    }
    commands_array_start++; // Move past ':'
    while (*commands_array_start && isspace((unsigned char)*commands_array_start)) commands_array_start++; // Skip whitespace
    if (*commands_array_start != '[') { // Check for opening bracket
        fprintf(stderr, "[Parser] Malformed JSON: 'commands' is not an array.\n");
        return -1;
    }
    commands_array_start++; // Move past '['

    const char* commands_array_end = strstr(commands_array_start, "]");
    if (!commands_array_end) {
        fprintf(stderr, "[Parser] Malformed 'commands' array: closing bracket not found.\n");
        return -1;
    }

    size_t commands_len = commands_array_end - commands_array_start;
    commands_substr = (char*)malloc(commands_len + 1);
    if (!commands_substr) {
        fprintf(stderr, "[Parser] Failed to allocate memory for commands parsing.\n");
        return -1;
    }

    strncpy(commands_substr, commands_array_start, commands_len);
    commands_substr[commands_len] = '\0';

    char* saveptr;
    char* token = strtok_r(commands_substr, ",", &saveptr);
    while (token != NULL && *command_count < MAX_COMMANDS) {
        // Trim leading/trailing whitespace
        while (isspace((unsigned char)*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        // Remove quotes
        if (*token == '"') token++;
        if (strlen(token) > 0 && *(token + strlen(token) - 1) == '"') {
            *(token + strlen(token) - 1) = '\0';
        }

        if (strlen(token) > 0) {
            if (parse_single_command_string(token, &commands[*command_count]) == 0) {
                (*command_count)++;
            } else {
                fprintf(stderr, "[Parser] Error: Failed to parse command token: '%s'\n", token);
                ret_val = -1;
                goto cleanup; // Go to cleanup on failure
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    // --- Parse snap_positions array ---
    const char* snaps_key_start = strstr(data_obj_start, "\"snap_positions\"");
    if (snaps_key_start) {
        const char* snaps_array_start_ptr = snaps_key_start + strlen("\"snap_positions\"");
        while (*snaps_array_start_ptr && isspace((unsigned char)*snaps_array_start_ptr)) snaps_array_start_ptr++;
        if (*snaps_array_start_ptr == ':') {
            snaps_array_start_ptr++;
            while (*snaps_array_start_ptr && isspace((unsigned char)*snaps_array_start_ptr)) snaps_array_start_ptr++;
            if (*snaps_array_start_ptr == '[') {
                 snaps_array_start_ptr++;
                const char* snaps_array_end = strstr(snaps_array_start_ptr, "]");
                if (snaps_array_end) {
                    size_t snaps_len = snaps_array_end - snaps_array_start_ptr;
                    snaps_substr = (char*)malloc(snaps_len + 1);
                    if (snaps_substr) {
                        strncpy(snaps_substr, snaps_array_start_ptr, snaps_len);
                        snaps_substr[snaps_len] = '\0';

                        char* snap_token = strtok_r(snaps_substr, "{", &saveptr);
                        while(snap_token != NULL && *snap_position_count < MAX_SNAP_POSITIONS) {
                            if (strchr(snap_token, '}')) {
                                if (get_json_int(snap_token, "x", &snap_positions[*snap_position_count].x) == 0 &&
                                    get_json_int(snap_token, "y", &snap_positions[*snap_position_count].y) == 0 &&
                                    get_json_int(snap_token, "d", &snap_positions[*snap_position_count].d) == 0) {
                                    (*snap_position_count)++;
                                }
                            }
                            snap_token = strtok_r(NULL, "{", &saveptr);
                        }
                    }
                }
            }
        }
    }

    ret_val = 0; // Success

cleanup:
    free(commands_substr);
    free(snaps_substr);
    return ret_val;
}