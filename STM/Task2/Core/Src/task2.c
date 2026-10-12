/*
 * task2.c
 *
 * Task 2 sequencer: the route of TASK2_DESIGN.md v2, driven from the tracked
 * pose (rear-axle centre; x forward from the carpark opening, y to the left,
 * heading in degrees, CCW +). Every distance is re-planned from where the
 * robot thinks it is, so drift seen by the gyro in one move is corrected by
 * the next. Constants: robot_config.h section 10. See task2.h for the commands.
 *
 * Everything here runs in the command task and blocks while the robot moves.
 */
#include "main.h"
#include "cmsis_os.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "robot_config.h"
#include "motion.h"
#include "task2.h"

#define RL  TURN_RADIUS_L
#define RR  TURN_RADIUS_R

static int   phase = 0;            // 0 wait GO, 1 wait arrow 1, 2 wait arrow 2, 3 parked
static int   a1    = 0;            // arrow 1: +1 left, -1 right
static float x2b   = 0.0f;         // obstacle 2 back face, tracked x
static float wallY = -1.0f;        // outer side wall |y| from the IR at scan 2, -1 = none in range
static char  failAt[12] = "";      // the move that was aborted

void Task2_Reset(void)
{
    phase  = 0;
    a1     = 0;
    wallY  = -1.0f;
    failAt[0] = '\0';
}

/* ---- Move wrappers: 1 = carry on. An abort ('!' / button) or a busy motor
 *      stops the run. A timeout doesn't: the robot has stopped, and the next
 *      move is planned from the tracked pose anyway. ----------------------- */
static int moveOk(const char *name)
{
    MoveResult_t r;
    Motion_LastResult(&r);
    if (r.end == MOVE_OK || r.end == MOVE_TIMEOUT) return 1;
    snprintf(failAt, sizeof failAt, "%s", name);
    return 0;
}

static int turn(int left, float deg)
{
    char name[8];
    if (deg < 0.5f) return 1;
    snprintf(name, sizeof name, "%cF%03d", left ? 'L' : 'R', (int)(deg + 0.5f));
    Motion_Turn(left, 1, deg);
    return moveOk(name);
}

static int turnBack(int left, float deg)            // reversing turn (LB / RB)
{
    char name[8];
    if (deg < 0.5f) return 1;
    snprintf(name, sizeof name, "%cB%03d", left ? 'L' : 'R', (int)(deg + 0.5f));
    Motion_Turn(left, -1, deg);
    return moveOk(name);
}

static int straight(float cm)                       // negative = backwards
{
    char name[8];
    if (fabsf(cm) < 0.5f) return 1;
    snprintf(name, sizeof name, "S%c%03d", (cm > 0.0f) ? 'F' : 'B', (int)(fabsf(cm) + 0.5f));
    Motion_Straight(cm);
    return moveOk(name);
}

static int sensorStraight(const Until_t *o, const char *name)
{
    Motion_StraightOpts(o);
    return moveOk(name);
}

/* ---- SE: along obstacle 2's face --------------------------------------------
 * The robot heads +y or -y beside obstacle 2's front or back face. It stops
 * where the next 90 deg turn (left if turnLeft, at radius R) puts it in the
 * lane beside the obstacle, on the first of (TASK2_DESIGN 2.2):
 *   - the IR on the turn side (it looks at the face) loses the face:
 *     drive (gap - R + IR_AHEAD) more  -> lane = obstacle end + gap
 *   - the ultrasonic reads the wall stop distance (the wall is ahead)
 *   - the lane cap, only if that IR never saw the face
 * and never before the lane floor, never past T2_LANE_HARD. If the wall is
 * already closer than the stop distance, it backs off instead.
 * useEdge = 0: ignore the IR (only cap / wall / floor).
 * vCap: speed. Above FACE_V_MAX the robot may run past the IR edge stop
 * point before it can stop; it then backs up to it.  Returns 1 to carry on. */
static int alongFace(int turnLeft, float capLane, int useEdge, float vCap)
{
    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    float dirn   = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;   // +1 = heading +y
    float R      = turnLeft ? RL : RR;
    float dw     = turnLeft ? T2_DW_L : T2_DW_R;
    float dCap   = (capLane - R) - dirn * y;             // travel to the cap lane
    float dFloor = (T2_LANE_FLOOR - R) - dirn * y;       // travel to the floor lane
    float dHard  = (T2_LANE_HARD - R) - dirn * y;        // travel to the hard lane cap

    int n;
    float u = Ultra_Query(3, &n);                        // standing still: is a wall already close?
    if (u > 0.0f) {
        float need = fmaxf(u - dw, dFloor);
        if (need < -0.5f) {
            return straight(need);                       // back off to the wall stop distance
        }
    }

    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm     = fminf(dCap, dHard);
    o.minCm     = dFloor;
    o.vCap      = vCap;
    o.usStop    = dw;
    if (useEdge) {
        o.edgeSide  = turnLeft ? IR_LEFT : IR_RIGHT;
        o.edgeExtra = (turnLeft ? T2_GAP_L : T2_GAP_R) - R + IR_AHEAD_CM;
        o.edgeMaxCm = dHard;
    }
    if (!sensorStraight(&o, turnLeft ? "SEL" : "SER")) return 0;

    MoveResult_t r;
    Motion_LastResult(&r);
    if (r.stopBy == 'E' && r.dist > r.target + T2_OVERSHOOT_FIX_CM) {
        return straight(-(r.dist - r.target));           // ran past the edge stop point: back up to it
    }
    return 1;
}

/* ---- Carpark entry plan (TASK2_DESIGN 4.1) -----------------------------------
 * From heading 180 at |y| off the centreline: turn alpha toward the
 * centreline (radius R), then one straight of length Lh ends with the tail
 * T2_M_IN inside the opening, parked at alpha. x0 = where the turn starts.
 * ya = where the rear axle should end, measured from the centreline toward
 * the side the robot comes from (0 = centred). */
static void entryPlan(float alDeg, float absY, float R, float ya, float *x0, float *Lh)
{
    float a  = alDeg * DEG2RAD;
    float xa = -(T2_M_IN + T2_REAR_CM * cosf(a) + T2_HALFW_CM * sinf(a));   // parked rear-axle x
    *Lh = (absY - R * (1.0f - cosf(a)) - ya) / sinf(a);  // ends ya from the centreline, on the start side
    *x0 = xa + R * sinf(a) + *Lh * cosf(a);
}

/* ---- GO: to scan 1 ------------------------------------------------------------ */
void Task2_Go(char *reply, size_t len)
{
    if (phase != 0) {
        snprintf(reply, len, "S1 ERR phase %d, send ZH", phase);
        return;
    }
    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    Until_t o;
    Motion_UntilInit(&o);
    o.usStop   = T2_D1;
    o.maxCm    = T2_SU1_MAX_CM;
    o.ignBelow = T2_IGN_BELOW_CM;                        // the carpark arms
    o.ignUntil = fmaxf(0.0f, T2_IGN_PAST_CM - (x + ROBOT_FRONT_CM));
    if (!sensorStraight(&o, "SU030")) {
        snprintf(reply, len, "S1 ABORT at %s", failAt);
        return;
    }
    MoveResult_t r;
    Motion_LastResult(&r);
    phase = 1;
    snprintf(reply, len, "S1 u=%.1f by=%c", r.u, r.stopBy);
}

/* ---- Arrow 1: dodge obstacle 1, lane to scan 2, outer wall from the IR ----- */
static int arrow1Moves(MoveResult_t *r)
{
    if (!turn(a1 > 0, T2_TH1)) return 0;
    if (!turn(a1 < 0, T2_TH1)) return 0;                 // in the lane, 21.7 off the centreline

    Until_t o;
    Motion_UntilInit(&o);
    o.usStop = T2_RSCAN;
    o.maxCm  = T2_SU2_MAX_CM;
    if (!sensorStraight(&o, "SU041")) return 0;
    Motion_LastResult(r);

    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    float u2 = (r->u > 0.0f) ? r->u : T2_RSCAN;          // no echo: assume the stop distance
    x2b = x + ROBOT_FRONT_CM + u2 + T2_OB_DEPTH;

    float ir[2];
    IR_Query(T2_WALL_QUERY_N, ir);
    float rOut = ir[(a1 > 0) ? IR_LEFT : IR_RIGHT];      // the side away from the centreline
    wallY = (rOut < IR_CLEAR) ? fabsf(y) + IR_LENS_FROM_CL_CM + rOut : -1.0f;
    return 1;
}

/* ---- Same side, no wall seen: find obstacle 2's end, then into the lane ------
 * Crosses in front of obstacle 2 exactly like the opposite-side routes
 * (arrow 1 R: SF011 + LF090, heading +y; arrow 1 L: RF090, heading -y), so
 * the inner IR is on the face. Then REVERSES along the face toward the side
 * it must pass, until the IR loses the face: that gives the obstacle end.
 * It drives back to the point where a reversing 90 deg turn (LB090 / RB090)
 * leaves it heading +x in the lane at end + T2_GAP_REV. A reversing turn
 * swings only ~0.2 cm past the robot's side, against 15-18 cm for a forward
 * turn, so nothing beyond the lane is touched (short wall, box or none). */
static int sameSideByFace(void)
{
    int toTop = (a1 > 0);                                // pass on the left = the +y end
    if (toTop) {
        if (!turn(0, 90.0f)) return 0;                   // RF090: heading -y, IR1 on the face
    } else {
        if (!straight(T2_XPASS)) return 0;               // clear obstacle 1 for the left turn
        if (!turn(1, 90.0f)) return 0;                   // LF090: heading +y, IR2 on the face
    }
    float x, y0, h;
    Motion_GetPose(&x, &y0, &h);
    float dirn = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;   // +1 = heading +y

    Until_t o;
    Motion_UntilInit(&o);
    o.reverse   = 1;
    o.vCap      = FACE_V_MAX;
    o.maxCm     = T2_REV_MAX_CM;
    o.edgeSide  = toTop ? IR_LEFT : IR_RIGHT;            // the side facing +x (obstacle 2)
    o.edgeExtra = 0.0f;                                  // stop as soon as the end is seen
    o.edgeMaxCm = T2_REV_MAX_CM;
    if (!sensorStraight(&o, "SBFACE")) return 0;
    MoveResult_t r;
    Motion_LastResult(&r);

    float R = toTop ? RR : RL;                           // RB090 / LB090
    float x1, y1;
    Motion_GetPose(&x1, &y1, &h);
    float yStop = y1;                                    // no end found: turn where it is
    if (r.stopBy == 'E') {
        // Axle where the lens passed the end (reversing moves against dirn),
        // then the end itself: the lens is IR_AHEAD_CM ahead of the axle.
        float yAxleEdge = y0 - dirn * r.target;
        float endY      = yAxleEdge + dirn * IR_AHEAD_CM;         // signed, on the pass side
        float lane      = endY + (toTop ? 1.0f : -1.0f) * T2_GAP_REV;
        yStop = toTop ? lane - R : lane + R;             // the reversing turn moves the axle R outward
    }
    if (!straight(dirn * (yStop - y1))) return 0;        // forward along the heading to yStop
    return turnBack(toTop ? 0 : 1, 90.0f);               // RB090 / LB090: heading +x, in the lane
}

/* ---- Arrow 2: round obstacle 2, loop behind it, return, park --------------- */
static int arrow2Moves(int a2)
{
    float x, y, h;
    Motion_GetPose(&x, &y, &h);

    // 1. Into the lane beside obstacle 2 (TASK2_DESIGN 3.2)
    if (a2 == a1) {
        float Y2 = -1.0f;
        if (wallY > 0.0f) {
            Y2 = wallY - T2_WALL_MARGIN - ((a1 < 0) ? T2_WALL_L : T2_WALL_R);
        }
        if (Y2 >= T2_LANE_FLOOR) {
            // Wall seen beside the robot: two arcs straight into the lane, Y2 from the wall
            float c   = 1.0f - (Y2 - fabsf(y)) / (RL + RR);
            float phi = acosf(fminf(fmaxf(c, 0.0f), 1.0f)) / DEG2RAD;
            if (!turn(a1 > 0, phi)) return 0;
            if (!turn(a1 < 0, phi)) return 0;
        } else {
            // No usable wall reading: the wall may be short (only beside
            // obstacle 2), a box, or absent, so the lane must come from the
            // obstacle's real end. Measured by reversing along the face.
            if (!sameSideByFace()) return 0;
        }
    } else {
        // Opposite side: cross in front of obstacle 2
        if (a1 < 0 && !straight(T2_XPASS)) return 0;     // R then L: clear obstacle 1 first
        if (!turn(a1 < 0, 90.0f)) return 0;
        if (!alongFace(a2 < 0, T2_LANE_CAP, 1, FACE_V_MAX)) return 0;
        if (!turn(a2 < 0, 90.0f)) return 0;
    }

    // 2. Past obstacle 2, then the loop behind it (left arrow = right turns)
    float gapLane;
    Motion_GetPose(&x, &gapLane, &h);
    if (!straight(x2b + T2_PAST - x)) return 0;
    int tl = (a2 < 0);
    if (!turn(tl, 90.0f)) return 0;
    if (!alongFace(tl, fmaxf(T2_LANE_CAP, fabsf(gapLane)), 1, T2_LOOP_V)) return 0;   // behind obstacle 2: faster
    if (!turn(tl, 90.0f)) return 0;                      // heading 180, back toward the carpark

    // 3. Return: one turn of alpha toward the centreline, one straight in (TASK2_DESIGN 4)
    Motion_GetPose(&x, &y, &h);
    int   s  = (y > 0.0f) ? 1 : -1;                      // +1 = top lane: turn left
    float R  = (s > 0) ? RL : RR;
    // The entry straight drifts to the robot's right, which is +y for both
    // entry directions (heading 200 or 160): aim that much toward -y.
    float ya = -(float)s * T2_ENTRY_DRIFT_CM;
    float al = T2_ALPHA, x0, Lh;
    entryPlan(al, fabsf(y), R, ya, &x0, &Lh);
    while (x0 > x - T2_TURN_LEAD && al < T2_ALPHA_MAX) { // no room for the turn: steepen
        al += 0.5f;
        entryPlan(al, fabsf(y), R, ya, &x0, &Lh);
    }
    if (!straight(x - x0)) return 0;                     // heading 180: forward = toward -x
    if (!turn(s > 0, al)) return 0;

    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm      = Lh;                                   // stop on tracked distance
    o.slowZoneCm = T2_ENTRY_SLOW_CM;                     // last 45 cm slowly
    o.vSlow      = FACE_V_MAX;
    o.guardSide  = (s > 0) ? IR_LEFT : IR_RIGHT;         // far-side IR: the other arm
    o.guardCm    = T2_GUARD_CM;
    o.guardFromCm = fmaxf(0.0f, Lh - T2_ENTRY_SLOW_CM);  // only near the arms (it would see obstacle 2 earlier)
    o.usEmergCm  = T2_US_EMERG_CM;
    return sensorStraight(&o, "ENTRY");
}

void Task2_Arrow(int left, char *reply, size_t len)
{
    int a = left ? 1 : -1;
    if (phase == 1) {
        a1 = a;
        MoveResult_t r;
        if (!arrow1Moves(&r)) {
            phase = 0;
            snprintf(reply, len, "S2 ABORT at %s", failAt);
            return;
        }
        phase = 2;
        snprintf(reply, len, "S2 u=%.1f w=%.1f by=%c", r.u, wallY, r.stopBy);
    } else if (phase == 2) {
        if (!arrow2Moves(a)) {
            phase = 0;
            snprintf(reply, len, "FIN ABORT at %s", failAt);
            return;
        }
        phase = 3;
        float x, y, h;
        Motion_GetPose(&x, &y, &h);
        MoveResult_t r;
        Motion_LastResult(&r);
        snprintf(reply, len, "FIN x=%.1f y=%.1f h=%+.1f by=%c", x, y, h, r.stopBy);
    } else {
        snprintf(reply, len, "T2 ERR phase %d", phase);
    }
}

/* ---- Bench: one SE leg with the default lane cap. Reply: Motion_Reply() ----- */
void Task2_AlongFace(int turnLeft)
{
    alongFace(turnLeft, T2_LANE_CAP, 1, FACE_V_MAX);
}
