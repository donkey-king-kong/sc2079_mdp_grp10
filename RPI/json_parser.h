#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "shared_types.h" // For Obstacle, Command, SnapPosition, SharedAppContext

// Function to extract an integer value from a JSON string for a given key
int get_json_int(const char *json_string, const char *key, int *value);

// Function to extract a string value from a JSON string for a given key
int get_json_string(const char *json_string, const char *key, char *value_buffer, size_t buffer_size);

// Function to parse the Android map JSON into the SharedAppContext
int parse_android_map_json(const char* json_string, SharedAppContext* context);

// Function to parse the pathfinding server's route response
int parse_route_json(const char* json_string, Command commands[], int* command_count, SnapPosition snap_positions[], int* snap_position_count);

#endif // JSON_PARSER_H
