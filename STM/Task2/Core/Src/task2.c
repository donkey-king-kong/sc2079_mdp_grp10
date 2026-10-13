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
static float endFound = -1.0f;     // same side: obstacle 2's end used for the lane (|y|, cm), -1 = not measured
static float aimDeg   = 0.0f;      // step 8: scan 2 aim turn still to undo (deg), 0 = none
static int   aimLeft  = 0;         // ... its steering side (1 = left lock)
static int   reTries  = 0;         // same side: ends rejected by the step 7 checks (FIN "re=")

void Task2_Reset(void)
{
    phase  = 0;
    a1     = 0;
    wallY  = -1.0f;
    failAt[0] = '\0';
    endFound = -1.0f;
    reTries  = 0;
    aimDeg   = 0.0f;
    aimLeft  = 0;
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

/* A clear straight at T2_FAST_V (no sensors). Backwards or tiny: normal straight. */
static int fastStraight(float cm)
{
    if (cm < 5.0f) return straight(cm);
    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm = cm;
    o.vCap  = T2_FAST_V;
    return sensorStraight(&o, "SFFAST");
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
    int   side   = turnLeft ? IR_LEFT : IR_RIGHT;
    float minLens = T2_EDGE_MIN_Y;                       // no end of obstacle 2 nearer the centreline than this

    int n;
    float u = Ultra_Query(3, &n);                        // standing still: is a wall already close?
    if (u > 0.0f) {
        float need = fmaxf(u - dw, (T2_LANE_FLOOR - R) - dirn * y);
        if (need < -0.5f) {
            return straight(need);                       // back off to the wall stop distance
        }
    }

    for (int attempt = 1; ; attempt++) {
        Motion_GetPose(&x, &y, &h);
        float dCap   = (capLane - R) - dirn * y;         // travel to the cap lane
        float dFloor = (T2_LANE_FLOOR - R) - dirn * y;   // travel to the floor lane
        float dHard  = (T2_LANE_HARD - R) - dirn * y;    // travel to the hard lane cap

        Until_t o;
        Motion_UntilInit(&o);
        o.maxCm     = (attempt == 1) ? fminf(dCap, dHard) : dHard;   // a retry starts on the face
        o.minCm     = dFloor;
        o.vCap      = vCap;
        o.usStop    = dw;
        if (useEdge) {
            o.edgeSide  = side;
            o.edgeExtra = (turnLeft ? T2_GAP_L : T2_GAP_R) - R + IR_AHEAD_CM;
            o.edgeMaxCm = dHard;
            // No end within T2_EDGE_MIN_Y of the centreline (the lens is
            // IR_AHEAD_CM ahead of the axle, moving toward the end it looks for)
            o.edgeFromCm = fmaxf(0.0f, minLens - (dirn * y + IR_AHEAD_CM));
        }
        if (!sensorStraight(&o, turnLeft ? "SEL" : "SER")) return 0;

        MoveResult_t r;
        Motion_LastResult(&r);
        if (r.stopBy == 'E' && attempt < T2_REV_TRIES) {
            // Step 7: stopped past the end, the IR must see nothing. Back on the
            // face = it was a seam / card edge: carry on along the face from here.
            float ir[2];
            IR_Query(T2_END_CHECK_N, ir);
            if (ir[side] < IR_FACE_CM) {
                reTries++;
                Motion_GetPose(&x, &y, &h);
                minLens = dirn * y + IR_AHEAD_CM;        // any end from here on
                continue;
            }
        }
        if (r.stopBy == 'E' && r.dist > r.target + T2_OVERSHOOT_FIX_CM) {
            return straight(-(r.dist - r.target));       // ran past the edge stop point: back up to it
        }
        return 1;
    }
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

/* ---- Scan 2 aim (step 8) ------------------------------------------------------
 * From the scan 2 stop (heading +x, |y| = the dodge lane), the forward turn
 * toward the centreline after which the camera, T2_CAM_AHEAD_CM ahead of the
 * rear axle, points straight at the card at (faceX, 0). Lock radius arc. */
static float scanAimDeg(float x, float y, float faceX)
{
    float s = (y < 0.0f) ? 1.0f : -1.0f;                 // +1 = right of the centreline: turn left
    float R = (s > 0.0f) ? RL : RR;
    float th;
    for (th = 0.0f; th < T2_AIM_MAX; th += 0.5f) {
        float t  = th * DEG2RAD;
        float cx = x + R * sinf(t) + T2_CAM_AHEAD_CM * cosf(t);
        float cy = y + s * (R * (1.0f - cosf(t)) + T2_CAM_AHEAD_CM * sinf(t));
        float b  = atan2f(-cy, faceX - cx) / DEG2RAD;    // bearing of the card from the camera
        if (th >= s * b) break;                          // now pointing at it
    }
    return th;
}

/* ---- Arrow 1: dodge obstacle 1, lane to scan 2, outer wall from the IR ----- */
static int arrow1Moves(MoveResult_t *r)
{
    // Dodge: two equal arcs into the lane T2_DODGE_LANE off the centreline,
    // measured from where the tracker says the robot is now (the long
    // approach to scan 1 can drift it a few cm sideways).
    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    float s   = (a1 > 0) ? 1.0f : -1.0f;
    float dy  = T2_DODGE_LANE - s * y;                   // sideways move needed
    float th  = acosf(fminf(fmaxf(1.0f - dy / (RL + RR), -1.0f), 1.0f)) / DEG2RAD;
    th = fminf(fmaxf(th, T2_TH1_MIN), T2_TH1_MAX);
    if (!turn(a1 > 0, th)) return 0;
    if (!turn(a1 < 0, th)) return 0;

    Until_t o;
    Motion_UntilInit(&o);
    o.usStop = T2_RSCAN;
    o.maxCm  = T2_SU2_MAX_CM;
    if (!sensorStraight(&o, "SU041")) return 0;
    Motion_LastResult(r);

    Motion_GetPose(&x, &y, &h);
    float u2 = (r->u > 0.0f) ? r->u : T2_RSCAN;          // no echo: assume the stop distance
    x2b = x + ROBOT_FRONT_CM + u2 + T2_OB_DEPTH;

    float ir[2];
    IR_Query(T2_WALL_QUERY_N, ir);
    float rOut = ir[(a1 > 0) ? IR_LEFT : IR_RIGHT];      // the side away from the centreline
    wallY = (rOut < IR_CLEAR) ? fabsf(y) + IR_LENS_FROM_CL_CM + rOut : -1.0f;

#if T2_SCAN2_AIM
    // Step 8: face the arrow card for the camera. A forward arc toward the
    // centreline (away from any wall; obstacle 2 stays >= 26 cm ahead), undone
    // by the same arc in reverse before arrow 2's moves, so every later move
    // starts from this stop exactly as before.
    aimLeft = (a1 < 0);                                  // right lane -> left turn
    float aim = scanAimDeg(x, y, x + ROBOT_FRONT_CM + u2);
    if (aim >= 0.5f) {
        if (!turn(aimLeft, aim)) return 0;
        aimDeg = aim;
    }
#endif
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
 * turn, so nothing beyond the lane is touched (short wall, box or none).
 *
 * Step 7 (13 Oct AR/AR: the IR "lost the face" at the edge of the image card,
 * ~5 cm off centre, so the lane was in front of obstacle 2):
 *  - an end only counts once the lens is T2_EDGE_MIN_Y past the centreline
 *    (obstacle 2 is >= 30 cm long, centred, card at the centre), and the IR
 *    must then read "nothing there" (IR_EDGE_FAR_CM, motion.c);
 *  - stopped there, the IR must still read nothing. Past a seam or the card's
 *    edge it is back on the face: that was not the end, so it reverses on;
 *  - after the reversing turn the ultrasonic must see nothing within
 *    T2_BLOCKED_CM ahead. If obstacle 2 is there, the robot retraces the turn
 *    (same arc, forwards) and searches on, past the rejected end. After
 *    T2_REV_TRIES it stops: "FIN ABORT at BLOCK" instead of a collision. */
static int sameSideByFace(void)
{
    int   toTop = (a1 > 0);                              // pass on the left = the +y end
    float p     = toTop ? 1.0f : -1.0f;                  // the pass side
    if (toTop) {
        if (!turn(0, 90.0f)) return 0;                   // RF090: heading -y, IR1 on the face
    } else {
        if (!straight(T2_XPASS)) return 0;               // clear obstacle 1 for the left turn
        if (!turn(1, 90.0f)) return 0;                   // LF090: heading +y, IR2 on the face
    }
    float x, y0, h;
    Motion_GetPose(&x, &y0, &h);
    float dirn   = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;   // +1 = heading +y; reversing moves toward p = -dirn
    float R      = toTop ? RR : RL;                      // RB090 / LB090
    float minEnd = T2_EDGE_MIN_Y;                        // the end must be at least this far out (pass side)

    int side = toTop ? IR_LEFT : IR_RIGHT;               // the side facing +x (obstacle 2)

    for (int attempt = 1; ; attempt++) {
        float lens0 = p * y0 + IR_AHEAD_CM * p * dirn;   // lens now, on the pass side (+ = past the centreline)
        Until_t o;
        Motion_UntilInit(&o);
        o.reverse    = 1;
        o.vCap       = FACE_V_MAX;
        o.maxCm      = T2_REV_MAX_CM;
        o.edgeSide   = side;
        o.edgeExtra  = T2_REV_PAST_CM;                   // stop a little past the end, so the checks look from beyond it
        o.edgeMaxCm  = T2_REV_MAX_CM;
        o.edgeFromCm = fmaxf(0.0f, minEnd - lens0);      // reversing moves the lens 1 cm out per cm
        if (!sensorStraight(&o, "SBFACE")) return 0;
        MoveResult_t r;
        Motion_LastResult(&r);

        float x1, y1;
        Motion_GetPose(&x1, &y1, &h);
        float yStop = y1;                                // no end found: turn where it is
        float endP  = -1.0f;
        if (r.stopBy == 'E') {
            // Axle where the lens passed the end (reversing moves against dirn),
            // then the end itself: the lens is IR_AHEAD_CM ahead of the axle.
            float yAxleEdge = y0 - dirn * (r.target - T2_REV_PAST_CM);
            float endY      = yAxleEdge + dirn * IR_AHEAD_CM;     // signed, on the pass side
            endP = p * endY;

            float ir[2];
            IR_Query(T2_END_CHECK_N, ir);                // standing still, ~T2_REV_PAST_CM past that "end"
            if (ir[side] < IR_FACE_CM) {                 // still a face beside the lens: not the end
                reTries++;
                if (attempt >= T2_REV_TRIES) {
                    snprintf(failAt, sizeof failAt, "BLOCK");
                    return 0;
                }
                y0     = y1;                             // reverse on from here ...
                minEnd = endP + T2_EDGE_RETRY_CM;        // ... past the rejected end
                continue;
            }
            float lane = endY + p * T2_GAP_REV;
            yStop = toTop ? lane - R : lane + R;         // the reversing turn moves the axle R outward
            endFound = endP;
        }
        if (!straight(dirn * (yStop - y1))) return 0;    // forward along the heading to yStop
        if (!turnBack(toTop ? 0 : 1, 90.0f)) return 0;   // RB090 / LB090: heading +x, in the lane

        // Obstacle 2 must not be ahead. In the right lane its corner is
        // ~50 deg off the ultrasonic's axis, so nothing is seen within 60 cm.
        int   n;
        float u = Ultra_Query(3, &n);
        if (!(u > 0.0f && u < T2_BLOCKED_CM)) return 1;  // clear: on to the pass
        reTries++;
        if (attempt >= T2_REV_TRIES || endP < 0.0f) {
            snprintf(failAt, sizeof failAt, "BLOCK");
            return 0;                                    // stop here: never drive into it
        }
        if (!turn(toTop ? 0 : 1, 90.0f)) return 0;       // retrace the reversing turn (same arc, forwards)
        Motion_GetPose(&x, &y0, &h);
        minEnd = endP + T2_EDGE_RETRY_CM;                // and search on, past the rejected end
    }
}

/* ---- Arrow 2: round obstacle 2, loop behind it, return, park --------------- */
static int arrow2Moves(int a2)
{
    float x, y, h;
    if (aimDeg >= 0.5f) {                                // step 8: undo the scan 2 aim (same arc, reversing)
        if (!turnBack(aimLeft, aimDeg)) return 0;
        aimDeg = 0.0f;
    }
    Motion_GetPose(&x, &y, &h);

    // 1. Into the lane beside obstacle 2 (TASK2_DESIGN 3.2)
    if (a2 == a1) {
        float Y2 = -1.0f;
        if (wallY > 0.0f) {
            Y2 = wallY - T2_WALL_MARGIN - ((a1 < 0) ? T2_WALL_L : T2_WALL_R);
        }
        if (T2_SAME_SIDE_USE_WALL && Y2 >= T2_LANE_FLOOR) {
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
    if (!fastStraight(x2b + T2_PAST - x)) return 0;
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
    if (!fastStraight(x - x0)) return 0;                 // heading 180: forward = toward -x
    if (!turn(s > 0, al)) return 0;

    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm      = Lh;                                   // stop on tracked distance
    o.vCap       = T2_FAST_V;
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
        snprintf(reply, len, "S2 u=%.1f w=%.1f by=%c a=%.1f", r.u, wallY, r.stopBy, aimDeg);
    } else if (phase == 2) {
        if (!arrow2Moves(a)) {
            phase = 0;
            snprintf(reply, len, "FIN ABORT at %s end=%.1f re=%d", failAt, endFound, reTries);
            return;
        }
        phase = 3;
        float x, y, h;
        Motion_GetPose(&x, &y, &h);
        MoveResult_t r;
        Motion_LastResult(&r);
        snprintf(reply, len, "FIN x=%.1f y=%.1f h=%+.1f by=%c end=%.1f re=%d", x, y, h, r.stopBy, endFound, reTries);
    } else {
        snprintf(reply, len, "T2 ERR phase %d", phase);
    }
}

/* ---- Bench: one SE leg with the default lane cap. Reply: Motion_Reply() ----- */
void Task2_AlongFace(int turnLeft)
{
    alongFace(turnLeft, T2_LANE_CAP, 1, FACE_V_MAX);
}
