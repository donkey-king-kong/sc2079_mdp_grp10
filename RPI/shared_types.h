#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @file shared_types.h
 * @brief Defines common data structures and types used across the RPi project.
 */

// Represents the overall state of the RPi's main navigation logic.
typedef enum {
    STATE_IDLE,
    STATE_PATHFINDING,
    STATE_NAVIGATING,
    STATE_ERROR
} SystemState;

// Represents a single obstacle from the Android map.
typedef struct {
    int id;
    int x;
    int y;
    int d; // Direction (0=N, 2=E, 4=S, 6=W)
} Obstacle;

// Represents the robot's position and direction at a snapshot event.
typedef struct {
    int x;
    int y;
    int d; // Direction (0=N, 2=E, 4=S, 6=W)
} SnapPosition;


// Represents a single command in the navigation route.
typedef enum {
    CMD_MOVE_FORWARD,
    CMD_MOVE_BACKWARD,
    CMD_TURN_LEFT,
    CMD_TURN_RIGHT,
    CMD_SNAPSHOT,
    CMD_FINISH
} CommandType;

typedef struct {
    CommandType type;
    int value; // For move commands, this is distance; for turn, this is angle; for snapshot, this is obstacle ID.
} Command;

#define MAX_OBSTACLES 20
#define MAX_COMMANDS 100
// An array to map integer directions to string representations for Android.
extern const char* DIR_MAP_ANDROID_STR[8];

// --- Threading and Shared State Management ---

#define MAX_SNAP_POSITIONS 100

// A structure to hold all application state that is shared between threads.
// Access to this struct MUST be protected by the mutex.
typedef struct {
    pthread_mutex_t lock;
    SystemState state;

    // Flags for cross-thread communication
    bool stop_requested;
    bool new_map_received;

    // Condition variable to signal the navigation thread that a new task is ready.
    pthread_cond_t new_task_cond;

    // Data for the current mission
    Obstacle obstacles[MAX_OBSTACLES];
    int obstacle_count;
    int robot_start_x;
    int robot_start_y;
    int robot_start_dir; // Initial robot direction for pathfinding
    Command commands[MAX_COMMANDS];
    int command_count;
    SnapPosition snap_positions[MAX_SNAP_POSITIONS]; // To store robot positions at snapshot events
    int snap_position_count; // Number of valid snap positions
    int snap_position_idx;

    // File descriptors needed by multiple threads
    int android_fd;       // Used for reading from Android
    int android_write_fd; // Used for writing to Android
    int stm32_fd;

    // STM32 ACK synchronization
    volatile uint32_t stm32_last_ack_id;
    pthread_mutex_t stm32_ack_mutex;
    pthread_cond_t stm32_ack_cond;

    // Image capture synchronization
    volatile uint32_t last_image_capture_id;
    pthread_mutex_t image_capture_mutex;
    pthread_cond_t image_capture_cond;

} SharedAppContext;

// --- Thread-specific arguments ---
typedef struct {
    SharedAppContext* context; // Pointer to the shared context
    int obstacle_id;
    SnapPosition robot_snap_position; // Robot's position at the time of snapshot
} ImageTaskArgs;

#endif // SHARED_TYPES_H
