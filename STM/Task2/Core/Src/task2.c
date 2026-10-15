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

#ifndef T2B_FLY_ENTRY
#define T2B_FLY_ENTRY 0     // Banzai: fly the entry turn too (robot_config.h); 0 = stop before it
#endif
#ifndef T2B_ALPHA_MAX
#define T2B_ALPHA_MAX T2_ALPHA_MAX
#endif
#ifndef T2_BLOCK_PINGS
#define T2_BLOCK_PINGS 3    // same side: ultrasonic pings in the "obstacle 2 ahead?" check after the reversing turn
#endif
#if BZ_WALL_SLALOM || BZ_SHORT_SLALOM
#define T2_WALL_IR_N 5      // Banzai: IR updates in the standing wall reading at scan 2 (the approach saw the rest)
#elif T2_SAME_SIDE_USE_WALL
#define T2_WALL_IR_N T2_WALL_QUERY_N
#else
#define T2_WALL_IR_N 0      // 14 Oct: nothing uses the wall reading: no standing IR query (was 11 updates, 0.22 s)
#endif

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
static int   wallOk   = 0;         // Banzai: a full wall was seen all along the approach to scan 2
static int   reTries  = 0;         // same side: ends rejected by the step 7 checks (FIN "re=")
static float x1f      = -1.0f;     // 14 Oct: obstacle 1's front face (tracked x) from scan 1, -1 = unknown
static float endSeen  = -1.0f;     // Banzai: |y| of obstacle 2's end seen by the front-face SE, -1 = none
static int   goFly    = 0;         // Banzai GL / GR: GO hands over at speed into the dodge
static float xScan2   = 0.0f;      // Banzai 3: rear-axle x at the scan 2 stop (before the aim)

/* ---- Move log (VT): name, how it ended, time since GO --------------------- */
#if T2_TRACE
#define T2_TRACE_N 44
static struct { char n[7]; char by; char f; uint16_t t; } trLog[T2_TRACE_N];
static int      trN  = 0;
static uint32_t trT0 = 0;
static void trStart(void) { trN = 0; trT0 = HAL_GetTick(); }
static void trAdd(const char *name, char by, char f)
{
    if (trN >= T2_TRACE_N) return;
    strncpy(trLog[trN].n, name, sizeof trLog[trN].n - 1);
    trLog[trN].n[sizeof trLog[trN].n - 1] = '\0';
    trLog[trN].by = by;
    trLog[trN].f  = f;
    uint32_t t = (HAL_GetTick() - trT0) / 10u;           // 10 ms units
    trLog[trN].t = (uint16_t)((t > 65535u) ? 65535u : t);
    trN++;
}
#else
#define trStart()        ((void)0)
#define trAdd(n, b, f)   ((void)0)
#endif

/* VT: the last run's moves on one line, integers only (no float printf):
 *   "TR n=<moves> <name>[/<stop by>][~ = handed over at speed | T = timed out | X = failed]@<s.cc> ..."
 * The time is when the move ENDED, in seconds since GO / GL / GR. "AL@" / "AR@" =
 * when the arrow command arrived (the gap before it = the Pi). */
void Task2_Trace(char *buf, size_t len)
{
#if T2_TRACE
    size_t k = (size_t)snprintf(buf, len, "TR n=%d", trN);
    for (int i = 0; i < trN && k + 20 < len; i++) {
        char tail[4] = "";
        int  m = 0;
        if (trLog[i].by) { tail[m++] = '/'; tail[m++] = trLog[i].by; }
        if (trLog[i].f)  { tail[m++] = trLog[i].f; }
        tail[m] = '\0';
        k += (size_t)snprintf(buf + k, len - k, " %s%s@%u.%02u", trLog[i].n, tail,
                              (unsigned)(trLog[i].t / 100u), (unsigned)(trLog[i].t % 100u));
    }
#else
    snprintf(buf, len, "TR off");
#endif
}

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
    wallOk   = 0;
    x1f      = -1.0f;
    endSeen  = -1.0f;
    goFly    = 0;
    xScan2   = 0.0f;
}

/* ---- Move wrappers: 1 = carry on. An abort ('!' / button) or a busy motor
 *      stops the run. A timeout doesn't: the robot has stopped, and the next
 *      move is planned from the tracked pose anyway. ----------------------- */
static int moveOkBy(const char *name, char by)
{
    MoveResult_t r;
    Motion_LastResult(&r);
    trAdd(name, by, (r.end != MOVE_OK && r.end != MOVE_TIMEOUT) ? 'X'
                    : ((Motion_ExitV() > 0.0f) ? '~' : ((r.end == MOVE_TIMEOUT) ? 'T' : 0)));
    if (r.end == MOVE_OK || r.end == MOVE_TIMEOUT) return 1;
    snprintf(failAt, sizeof failAt, "%s", name);
    return 0;
}

static int moveOk(const char *name)
{
    return moveOkBy(name, 0);
}

static int turn(int left, float deg)
{
    char name[8];
    if (deg < 0.5f) { Motion_EndSteer(-1); Motion_Fly(0, 0); return 1; }
    snprintf(name, sizeof name, "%cF%03d", left ? 'L' : 'R', (int)(deg + 0.5f));
    Motion_Turn(left, 1, deg);
    return moveOk(name);
}

static int turnBack(int left, float deg)            // reversing turn (LB / RB)
{
    char name[8];
    if (deg < 0.5f) { Motion_EndSteer(-1); Motion_Fly(0, 0); return 1; }
    snprintf(name, sizeof name, "%cB%03d", left ? 'L' : 'R', (int)(deg + 0.5f));
    Motion_Turn(left, -1, deg);
    return moveOk(name);
}

static int straight(float cm)                       // negative = backwards
{
    char name[8];
    if (fabsf(cm) < 0.5f) { Motion_EndSteer(-1); Motion_Fly(0, 0); return 1; }
    snprintf(name, sizeof name, "S%c%03d", (cm > 0.0f) ? 'F' : 'B', (int)(fabsf(cm) + 0.5f));
    Motion_Straight(cm);
    return moveOk(name);
}

static int sensorStraight(const Until_t *o, const char *name)
{
    Motion_StraightOpts(o);
    MoveResult_t r;
    Motion_LastResult(&r);
    return moveOkBy(name, r.stopBy);
}

/* Banzai 3: from the scan 2 stop, can the crossing LEFT turn (right lane) start at
 * once? Step 9 drives T2_XPASS first so its inner side clears obstacle 1: at
 * d2 = 60 the axle stops 3 cm short of obstacle 1's back face, so the turn needs
 * the axle >= (T2_XPASS - 3) past it. At d2 >= ~71 the scan 2 stop already is. */
static int pastXpass(void)
{
#if BZ_FLY_ALL
    return (x1f > 0.0f && xScan2 >= x1f + 10.0f + T2_XPASS - 3.0f);
#else
    return 0;
#endif
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
static int alongFace(int turnLeft, float capLane, int useEdge, float vCap, float expectEnd)
{
    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    float dirn   = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;   // +1 = heading +y
    float R      = turnLeft ? RL : RR;
    float dw     = turnLeft ? T2_DW_L : T2_DW_R;
    int   side   = turnLeft ? IR_LEFT : IR_RIGHT;
    float minLens = T2_EDGE_MIN_Y;                       // no end of obstacle 2 nearer the centreline than this
    if (expectEnd > 0.0f)                                // 15 Oct: obstacle 2 is centred, so the far end mirrors the measured
        minLens = fmaxf(minLens, expectEnd - T2_END_TOL_CM);   // one: a gap between boxes short of it is not the end (80 cm hit)

#if !BZ_NO_CHECKS
    int n;
    float u = Ultra_Query(3, &n);                        // standing still: is a wall already close?
    if (u > 0.0f) {
        float need = fmaxf(u - dw, (T2_LANE_FLOOR - R) - dirn * y);
        if (need < -0.5f) {
            Motion_EndSteer(turnLeft);                   // step 9: the caller's turn comes next
            return straight(need);                       // back off to the wall stop distance
        }
    }
#endif

    for (int attempt = 1; ; attempt++) {
        Motion_GetPose(&x, &y, &h);
        float dCap   = (capLane - R) - dirn * y;         // travel to the cap lane
        float dFloor = (T2_LANE_FLOOR - R) - dirn * y;   // travel to the floor lane
        if (expectEnd > 0.0f)                            // 15 Oct: and never short of the mirrored end: an early echo
            dFloor = fmaxf(dFloor, (expectEnd + (turnLeft ? T2_GAP_L : T2_GAP_R) - T2_END_TOL_CM - R) - dirn * y);   // (80 cm hit)
        float dHard  = (T2_LANE_HARD - R) - dirn * y;    // travel to the hard lane cap

        Until_t o;
        Motion_UntilInit(&o);
        o.maxCm     = (attempt == 1) ? fminf(dCap, dHard) : fminf(T2_RECHECK_CM, dHard);   // a retry starts on the face
        o.minCm     = dFloor;
        o.vCap      = vCap;
        o.usStop    = dw;
#if BZ_FAST
        if (useEdge && attempt == 1 && expectEnd > 0.0f && vCap > T2B_EDGE_V) {
            // Banzai: the end it looks for is predictable (obstacle 2 is centred and its
            // other end was measured): fast most of the way, T2B_EDGE_V near it.
            float tEnd = expectEnd - dirn * y - IR_AHEAD_CM;   // travel until the lens reaches that end
            o.slowFromCm = fmaxf(0.0f, tEnd - T2B_EDGE_SLOW_CM);
            o.vSlow      = T2B_EDGE_V;
            if (o.slowFromCm < 5.0f) { o.slowFromCm = 0.0f; o.vCap = T2B_EDGE_V; }
        }
#else
        (void)expectEnd;
#endif
        if (useEdge) {
            o.edgeSide  = side;
            o.edgeExtra = (turnLeft ? T2_GAP_L : T2_GAP_R) - R + IR_AHEAD_CM;
            o.edgeMaxCm = dHard;
            // No end within T2_EDGE_MIN_Y of the centreline (the lens is
            // IR_AHEAD_CM ahead of the axle, moving toward the end it looks for)
            o.edgeFromCm = fmaxf(0.0f, minLens - (dirn * y + IR_AHEAD_CM));
        }
#if BZ_FLY_ALL
        // Banzai 3: rolls in from the turn before and hands over at speed to the turn
        // after. That turn bites T2B_FLY_SWING_CM late (the servo swings while rolling),
        // so every stop point comes that much early.
        o.maxCm     -= T2B_FLY_SWING_CM;
        o.minCm     -= T2B_FLY_SWING_CM;
        o.edgeMaxCm -= T2B_FLY_SWING_CM;
        o.edgeExtra -= T2B_FLY_SWING_CM;
        o.usStop    += T2B_FLY_SWING_CM;
        Motion_Fly(1, 1);
#endif
        Motion_EndSteer(turnLeft);                       // step 9: the caller turns this way next
        if (!sensorStraight(&o, turnLeft ? "SEL" : "SER")) return 0;

        MoveResult_t r;
        Motion_LastResult(&r);
#if !BZ_NO_CHECKS
        if (attempt > 1 && !r.faceSeen) {                // step 9: the face never came back: the last stop was right
            Motion_EndSteer(turnLeft);
            return straight(-r.dist);
        }
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
#else
        (void)attempt;                                   // Banzai 3: one pass, the first end is the end
#endif
        if (r.stopBy == 'E') {                           // where the end was: lens at the edge point
            endSeen = fabsf(y + dirn * (r.target - o.edgeExtra + IR_AHEAD_CM));
        }
#if !BZ_NO_CHECKS
        if (r.stopBy == 'E' && r.dist > r.target + T2_OVERSHOOT_FIX_CM) {
            Motion_EndSteer(turnLeft);
            return straight(-(r.dist - r.target));       // ran past the edge stop point: back up to it
        }
#endif
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

#if T2_OB1_GUARD
/* Distance from point (px, py) to obstacle 1 (10 x 10 cm, front face at x1f, centred). */
static float ob1Dist(float px, float py)
{
    float dx = fmaxf(fmaxf(x1f - px, 0.0f), px - (x1f + 10.0f));
    float dy = fmaxf(fabsf(py) - 5.0f, 0.0f);
    return sqrtf(dx * dx + dy * dy);
}

/* Smallest gap between the robot's outline and obstacle 1 along the planned
 * return: from (x, y) heading 180 straight to x0, the turn of al (radius R,
 * toward the centreline, s = +1 top lane), then Lh straight. Sampled every 3 cm
 * (Banzai 3: cheaper, it runs while a flown turn hands over). */
static float entryOb1Clear(float x, float y, float R, int s, float al, float x0, float Lh)
{
    float best = 1.0e9f;
    float a    = al * DEG2RAD;
    float sg   = (s > 0) ? 1.0f : -1.0f;                 // +1 = left turn
    float arc  = R * a;
    float len  = fmaxf(0.0f, x - x0) + arc + fmaxf(0.0f, Lh);
    for (float t = 0.0f; t <= len + 0.01f; t += 3.0f) {
        float px, py, ph;
        float t1 = fmaxf(0.0f, x - x0);
        if (t <= t1) {                                   // the return straight
            px = x - t; py = y; ph = 3.14159265f;
        } else if (t <= t1 + arc) {                      // the turn
            float phi = (t - t1) / R;
            float hh  = 3.14159265f + sg * phi;
            px = x0 + sg * R * sinf(hh);
            py = (y - sg * R) - sg * R * cosf(hh);
            ph = hh;
        } else {                                         // the entry straight
            float he = 3.14159265f + sg * a;
            float ex = x0 + sg * R * sinf(he);
            float ey = (y - sg * R) - sg * R * cosf(he);
            float d  = t - t1 - arc;
            px = ex + d * cosf(he); py = ey + d * sinf(he); ph = he;
        }
        if (fabsf(px - (x1f + 5.0f)) > 45.0f) continue;   // nowhere near it
        float c = cosf(ph), sn = sinf(ph);
        for (float lx = -T2_REAR_CM; lx <= ROBOT_FRONT_CM + 0.01f; lx += 2.0f) {
            for (int k = -1; k <= 1; k += 2) {
                float ly = k * T2_HALFW_CM;
                float d  = ob1Dist(px + lx * c - ly * sn, py + lx * sn + ly * c);
                if (d < best) best = d;
            }
        }
        for (float ly = -T2_HALFW_CM; ly <= T2_HALFW_CM + 0.01f; ly += 2.0f) {
            float d = ob1Dist(px + ROBOT_FRONT_CM * c - ly * sn, py + ROBOT_FRONT_CM * sn + ly * c);
            if (d < best) best = d;
        }
    }
    return best;
}
#endif

/* ---- GO: to scan 1 ------------------------------------------------------------ */
static int goMoves(MoveResult_t *r, int fly)
{
    float x, y, h;
    Motion_GetPose(&x, &y, &h);
    Until_t o;
    Motion_UntilInit(&o);
    o.usStop   = T2_D1;
    o.maxCm    = T2_SU1_MAX_CM;
    o.ignBelow = T2_IGN_BELOW_CM;                        // the carpark arms
    o.ignUntil = fmaxf(0.0f, T2_IGN_PAST_CM - (x + ROBOT_FRONT_CM));
#if BZ_FAST
    o.vCap     = T2B_SU_V;                               // Banzai: obstacle 1 is dead ahead, a strong echo
#endif
#if BZ_FLY
    if (fly) {
        // GL / GR: no stop at scan 1. Hand over at speed T2B_FLY_SWING_CM early:
        // the first dodge arc bites that much later (servo still swinging).
        o.usStop += T2B_FLY_SWING_CM;
        Motion_Fly(0, 1);
    }
#else
    (void)fly;
#endif
    if (!sensorStraight(&o, "SU030")) return 0;
    Motion_LastResult(r);
    Motion_GetPose(&x, &y, &h);
    float u1 = (r->u > 0.0f) ? r->u : o.usStop;
    x1f = x + ROBOT_FRONT_CM + u1;                       // obstacle 1's front face, for the carpark entry check
    return 1;
}

void Task2_Go(char *reply, size_t len)
{
    if (phase != 0) {
        snprintf(reply, len, "S1 ERR phase %d, send ZH", phase);
        return;
    }
    MoveResult_t r;
    trStart();
    trAdd("GO", 0, 0);
    if (!goMoves(&r, 0)) {
        snprintf(reply, len, "S1 ABORT at %s", failAt);
        return;
    }
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
#if BZ_FLY
    if (goFly) Motion_Fly(1, 0);                         // GL / GR: rolls in from the approach to obstacle 1
#endif
    if (!turn(a1 > 0, th)) return 0;
#if BZ_FLY
    Motion_Fly(0, 1);                                    // Banzai: the 2nd arc rolls straight on to scan 2
#endif
    if (!turn(a1 < 0, th)) return 0;

    Until_t o;
    Motion_UntilInit(&o);
    o.usStop = T2_RSCAN;
    o.maxCm  = T2_SU2_MAX_CM;
    if (x1f > 0.0f) {
        // 14 Oct: never past the scan 2 stop for the farthest obstacle 2 the rules
        // allow, even if the ultrasonic never sees it (a 30 cm obstacle's face does
        // not reach the 23.5 lane: only its corner echoes, now and then).
        Motion_GetPose(&x, &y, &h);
        float cap = (x1f + 10.0f + T2_D2_MAX_CM - T2_RSCAN - ROBOT_FRONT_CM) - x;
        o.maxCm = fmaxf(1.0f, fminf(o.maxCm, cap));
    }
#if BZ_WALL_SLALOM
    o.watchSide = (a1 > 0) ? IR_LEFT : IR_RIGHT;         // Banzai: is there a full wall along the outer side?
    o.watchTol  = T2B_WALL_TOL;
#endif
#if T2_SCAN2_AIM
    Motion_EndSteer(a1 < 0);                             // step 9: steer for the aim turn while stopping
#endif
#if BZ_FLY
    Motion_Fly(1, 0);
#endif
    if (!sensorStraight(&o, "SU041")) return 0;
    Motion_LastResult(r);

    Motion_GetPose(&x, &y, &h);
    float u2 = (r->u > 0.0f) ? r->u : T2_RSCAN;          // no echo: assume the stop distance
    x2b = x + ROBOT_FRONT_CM + u2 + T2_OB_DEPTH;
    xScan2 = x;

    wallY = -1.0f;
#if T2_WALL_IR_N > 0
    float ir[2];
    IR_Query(T2_WALL_IR_N, ir);
    float rOut = ir[(a1 > 0) ? IR_LEFT : IR_RIGHT];      // the side away from the centreline
    wallY = (rOut < IR_CLEAR) ? fabsf(y) + IR_LENS_FROM_CL_CM + rOut : -1.0f;
#endif
#if BZ_WALL_SLALOM
    // Banzai: a FULL wall = the outer IR read the same distance over at least
    // T2B_WALL_CONFIRM_CM of the approach, and it still does here. A person,
    // bag or table leg is shorter; boards only beside obstacle 2 are never seen
    // here, so they can't trigger it (those cases take the reverse route).
    wallOk = (wallY >= T2B_WALL_MIN_Y && r->watchSpan >= T2B_WALL_CONFIRM_CM &&
              fabsf((wallY - fabsf(y) - IR_LENS_FROM_CL_CM) - r->watchMean) <= T2B_WALL_TOL);
    if (wallOk) wallY = fabsf(y) + IR_LENS_FROM_CL_CM + r->watchMean;   // the mean of >= 40 cm of readings: steadier
#endif

#if T2_SCAN2_AIM
    // Step 8: face the arrow card for the camera. A forward arc toward the
    // centreline (away from any wall; obstacle 2 stays >= 26 cm ahead), undone
    // by the same arc in reverse before arrow 2's moves, so every later move
    // starts from this stop exactly as before.
    aimLeft = (a1 < 0);                                  // right lane -> left turn
    float aim = scanAimDeg(x, y, x + ROBOT_FRONT_CM + u2);
    if (aim >= 0.5f) {
        Motion_EndSteer(aimLeft);                        // step 9: the undo uses the same lock
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
#if BZ_NO_CHECKS
/* Banzai 3: the same route in one pass. The first end the IR reports is the end
 * (still never within T2_EDGE_MIN_Y of the centreline: the image card): no
 * standing IR re-check, no "obstacle 2 ahead?" ultrasonic check, no retrace or
 * search-on. If the IR never finds an end, it turns where the reverse stopped. */
static int sameSideByFace(void)
{
    int   toTop = (a1 > 0);                              // pass on the left = the +y end
    float p     = toTop ? 1.0f : -1.0f;                  // the pass side
    if (toTop) {
        if (!turn(0, 90.0f)) return 0;                   // RF090: heading -y, IR1 on the face
    } else {
        Motion_EndSteer(1);                              // step 9: LF090 next
        if (!pastXpass()) {
#if BZ_FLY_ALL
            Motion_Fly(0, 1);                            // Banzai 3: rolls straight into LF090
#endif
            if (!straight(T2_XPASS)) return 0;           // clear obstacle 1 for the left turn
#if BZ_FLY_ALL
            Motion_Fly(1, 0);
#endif
        }
        if (!turn(1, 90.0f)) return 0;                   // LF090: heading +y, IR2 on the face
    }
    float x, y0, h;
    Motion_GetPose(&x, &y0, &h);
    float dirn  = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;    // +1 = heading +y; reversing moves toward p = -dirn
    float R     = toTop ? RR : RL;                       // RB090 / LB090
    int   side  = toTop ? IR_LEFT : IR_RIGHT;            // the side facing +x (obstacle 2)
    float lens0 = p * y0 + IR_AHEAD_CM * p * dirn;       // lens now, on the pass side (+ = past the centreline)

    Until_t o;
    Motion_UntilInit(&o);
    o.reverse    = 1;
    o.vCap       = T2_REV_V;
    o.maxCm      = T2_REV_MAX_CM;
    o.edgeSide   = side;
    o.edgeExtra  = T2_REV_PAST_CM;
    o.edgeMaxCm  = T2_REV_MAX_CM;
    o.edgeFromCm = fmaxf(0.0f, T2_EDGE_MIN_Y - lens0);   // reversing moves the lens 1 cm out per cm
    if (!sensorStraight(&o, "SBFACE")) return 0;
    MoveResult_t r;
    Motion_LastResult(&r);

    float x1, y1;
    Motion_GetPose(&x1, &y1, &h);
    float yStop = y1;                                    // no end found: turn where it is
    if (r.stopBy == 'E') {
        float yAxleEdge = y0 - dirn * (r.target - T2_REV_PAST_CM);
        float endY      = yAxleEdge + dirn * IR_AHEAD_CM;   // signed, on the pass side
        float lane      = endY + p * T2_GAP_REV;
        yStop    = toTop ? lane - R : lane + R;          // the reversing turn moves the axle R outward
        endFound = p * endY;
    }
    Motion_EndSteer(toTop ? 0 : 1);                      // RB090 / LB090 next
    if (!straight(dirn * (yStop - y1))) return 0;        // forward along the heading to yStop
    return turnBack(toTop ? 0 : 1, 90.0f);               // RB090 / LB090: heading +x, in the lane
}
#else
static int sameSideByFace(void)
{
    int   toTop = (a1 > 0);                              // pass on the left = the +y end
    float p     = toTop ? 1.0f : -1.0f;                  // the pass side
    if (toTop) {
        if (!turn(0, 90.0f)) return 0;                   // RF090: heading -y, IR1 on the face
    } else {
        Motion_EndSteer(1);                              // step 9: LF090 next
        if (!straight(T2_XPASS)) return 0;               // clear obstacle 1 for the left turn
        if (!turn(1, 90.0f)) return 0;                   // LF090: heading +y, IR2 on the face
    }
    float x, y0, h;
    Motion_GetPose(&x, &y0, &h);
    float dirn   = (sinf(h * DEG2RAD) > 0.0f) ? 1.0f : -1.0f;   // +1 = heading +y; reversing moves toward p = -dirn
    float R      = toTop ? RR : RL;                      // RB090 / LB090
    float minEnd = T2_EDGE_MIN_Y;                        // the end must be at least this far out (pass side)

    int   side      = toTop ? IR_LEFT : IR_RIGHT;        // the side facing +x (obstacle 2)
    float noFaceCap = T2_REV_MAX_CM;                     // reverse limit while the IR sees no face
    int   havePrev  = 0;                                 // step 9: an end was rejected by the standing check ...
    float prevEndY  = 0.0f;                              // ... here (signed y)

    for (int attempt = 1; ; attempt++) {
        float lens0 = p * y0 + IR_AHEAD_CM * p * dirn;   // lens now, on the pass side (+ = past the centreline)
        Until_t o;
        Motion_UntilInit(&o);
        o.reverse    = 1;
        o.vCap       = T2_REV_V;                         // step 9: 35 cm/s (was 25)
        o.maxCm      = noFaceCap;
        o.edgeSide   = side;
        o.edgeExtra  = T2_REV_PAST_CM;                   // stop a little past the end, so the checks look from beyond it
        o.edgeMaxCm  = T2_REV_MAX_CM;
        o.edgeFromCm = fmaxf(0.0f, minEnd - lens0);      // reversing moves the lens 1 cm out per cm
        if (!sensorStraight(&o, "SBFACE")) return 0;
        MoveResult_t r;
        Motion_LastResult(&r);

        float x1, y1;
        Motion_GetPose(&x1, &y1, &h);
        float yStop   = y1;                              // no end found: turn where it is
        float endP    = -1.0f;
        int   haveEnd = 0;
        float endY    = 0.0f;
        if (r.stopBy == 'E') {
            // Axle where the lens passed the end (reversing moves against dirn),
            // then the end itself: the lens is IR_AHEAD_CM ahead of the axle.
            float yAxleEdge = y0 - dirn * (r.target - T2_REV_PAST_CM);
            endY = yAxleEdge + dirn * IR_AHEAD_CM;       // signed, on the pass side
            endP = p * endY;

            float ir[2];
            IR_Query(T2_END_CHECK_N, ir);                // standing still, ~T2_REV_PAST_CM past that "end"
            if (ir[side] < IR_FACE_CM) {                 // still a face beside the lens: not the end
                reTries++;
                if (attempt >= T2_REV_TRIES) {
                    snprintf(failAt, sizeof failAt, "BLOCK");
                    return 0;
                }
                y0        = y1;                          // reverse on from here ...
                minEnd    = endP + T2_EDGE_RETRY_CM;     // ... past the rejected end,
                noFaceCap = T2_RECHECK_CM;               // ... but only a little unless the face is really there
                havePrev  = 1;
                prevEndY  = endY;
                continue;
            }
            haveEnd = 1;
        } else if (havePrev && !r.faceSeen) {
            // Step 9: the face never came back, so the rejected end was the real
            // one (the standing check saw something else). Use it.
            endY    = prevEndY;
            endP    = p * endY;
            haveEnd = 1;
        }
        if (haveEnd) {
            float lane = endY + p * T2_GAP_REV;
            yStop = toTop ? lane - R : lane + R;         // the reversing turn moves the axle R outward
            endFound = endP;
        }
        Motion_EndSteer(toTop ? 0 : 1);                  // step 9: RB090 / LB090 next
        if (!straight(dirn * (yStop - y1))) return 0;    // forward along the heading to yStop
        if (!turnBack(toTop ? 0 : 1, 90.0f)) return 0;   // RB090 / LB090: heading +x, in the lane

        // Obstacle 2 must not be ahead. In the right lane its corner is
        // ~50 deg off the ultrasonic's axis, so nothing is seen within 60 cm.
        int   n;
        float u = Ultra_Query(T2_BLOCK_PINGS, &n);
        if (!(u > 0.0f && u < T2_BLOCKED_CM)) return 1;  // clear: on to the pass
        reTries++;
        if (attempt >= T2_REV_TRIES || endP < 0.0f) {
            snprintf(failAt, sizeof failAt, "BLOCK");
            return 0;                                    // stop here: never drive into it
        }
        if (!turn(toTop ? 0 : 1, 90.0f)) return 0;       // retrace the reversing turn (same arc, forwards)
        Motion_GetPose(&x, &y0, &h);
        minEnd    = endP + T2_EDGE_RETRY_CM;             // and search on, past the rejected end
        noFaceCap = T2_RECHECK_RETRACE_CM;               // the lens is back on the face: it must see it at once
        havePrev  = 0;
    }
}

#endif /* BZ_NO_CHECKS */

/* ---- Arrow 2: round obstacle 2, loop behind it, return, park --------------- */
static int arrow2Moves(int a2)
{
    float x, y, h;
    int crossFromAim = 0;
#if BZ_FLY_ALL
    // Banzai 3, opposite side: the crossing 90 deg turn turns toward the centreline on
    // the same lock as the aim, from the same stop. So the aim is simply its first
    // part: carry on round the circle instead of reversing it out and starting again.
    // Right lane: only once the scan 2 stop is far enough past obstacle 1 to skip SF011.
    if (aimDeg >= 0.5f && a2 != a1 && (a1 > 0 || pastXpass())) crossFromAim = 1;
#endif
    if (aimDeg >= 0.5f && !crossFromAim) {               // step 8: undo the scan 2 aim (same arc, reversing)
        if (a1 > 0) Motion_EndSteer(0);                  // step 9: left lane: RF090 comes next on every route
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
        int slalom = ((T2_SAME_SIDE_USE_WALL || wallOk) && Y2 >= T2_LANE_FLOOR);
#if BZ_SHORT_SLALOM
        if (!slalom) {
            // Banzai 2: standing at the scan 2 stop, heading straight. If obstacle 2's
            // face reached the robot's lane, the ultrasonic (on the robot's centreline)
            // would get a square echo from it on every ping. Few or no echoes = its end
            // is inside the lane (|end| < ~23 cm): slalom forward to T2B_SHORT_LANE_L/_R, no
            // reverse search. A board beside such a short obstacle may stand 65 cm out, unseen
            // from here: the lane splits the room between it and the obstacle's end.
            // A wall / board the outer IR does see caps the lane.
#if BZ_NO_CHECKS
            // Banzai 3: only echoes from about obstacle 2's distance count. Something far
            // behind it (a wall, people) used to count as "the face" and sent short
            // obstacles down the slow reverse route.
            float faceD = (x2b - T2_OB_DEPTH) - (x + ROBOT_FRONT_CM);
            int   n     = Ultra_CountNear(T2B_SHORT_PINGS, faceD + 15.0f);
#else
            int   n;
            (void)Ultra_Query(T2B_SHORT_PINGS, &n);
#endif
            if (n <= T2B_SHORT_MAX_ECHO) {
                float Ys = (a1 > 0) ? T2B_SHORT_LANE_L : T2B_SHORT_LANE_R;
                if (Y2 > 0.0f) Ys = fminf(Ys, Y2);       // a wall / board seen beside the robot
                if (Ys >= T2_LANE_FLOOR) {
                    Y2 = Ys;
                    slalom = 1;
                }
            }
        }
#endif
        if (slalom) {
            // Wall seen beside the robot (or a short obstacle 2): two arcs straight
            // into the lane Y2 (Banzai: wall confirmed all along the approach, wallOk)
            float c   = 1.0f - (Y2 - fabsf(y)) / (RL + RR);
            float phi = acosf(fminf(fmaxf(c, 0.0f), 1.0f)) / DEG2RAD;
            if (!turn(a1 > 0, phi)) return 0;
#if BZ_FLY
            Motion_Fly(0, 1);                            // Banzai: straight on into the pass, no stop
#endif
            if (!turn(a1 < 0, phi)) return 0;
        } else {
            // No usable wall reading: the wall may be short (only beside
            // obstacle 2), a box, or absent, so the lane must come from the
            // obstacle's real end. Measured by reversing along the face.
            if (!sameSideByFace()) return 0;
        }
    } else {
        // Opposite side: cross in front of obstacle 2
        if (crossFromAim) {
            // Banzai 3: (90 - aim) more round the aim's circle = the crossing turn
            Motion_Fly(0, 1);                            // ... rolls straight on into the SE along the face
            if (!turn(aimLeft, 90.0f - aimDeg)) return 0;
            aimDeg = 0.0f;
        } else {
            if (a1 < 0) Motion_EndSteer(1);              // step 9: LF090 next
            if (a1 < 0 && !pastXpass()) {                // R then L: clear obstacle 1 first
#if BZ_FLY_ALL
                Motion_Fly(0, 1);                        // Banzai 3: rolls straight into LF090
#endif
                if (!straight(T2_XPASS)) return 0;
            }
#if BZ_FLY_ALL
            Motion_Fly(1, 1);                            // Banzai 3: ... and on into the SE
#endif
            if (!turn(a1 < 0, 90.0f)) return 0;
        }
        if (!alongFace(a2 < 0, T2_LANE_CAP, 1, T2_FACE_V, -1.0f)) return 0;
#if BZ_FLY
        Motion_Fly(BZ_FLY_ALL, 1);                       // Banzai: straight on into the pass, no stop
#endif                                                   //   (Banzai 3: rolls in from the SE too)
        if (!turn(a2 < 0, 90.0f)) return 0;
    }

    // 2. Past obstacle 2, then the loop behind it (left arrow = right turns)
    float gapLane;
    Motion_GetPose(&x, &gapLane, &h);
    int tl = (a2 < 0);
    Motion_EndSteer(tl);                                 // step 9: the loop turn next
#if BZ_FLY
    Motion_Fly(1, 1);                                    // Banzai: no stop into or out of the pass (flyIn only
#endif                                                   //   acts if the last move handed over at speed)
    if (!fastStraight(x2b + T2_PAST - x)) return 0;
#if BZ_FLY
    Motion_Fly(1, BZ_FLY_ALL);                           // loop turn 1 rolls in (Banzai 3: and on into the SE behind)
#endif
    if (!turn(tl, 90.0f)) return 0;
    {   // Behind obstacle 2. Banzai: fast with a slow-down near the far end when it
        // is predictable (one end measured: the reverse search or the front face).
        float endKnown = (endFound > 0.0f) ? endFound : endSeen;
        float vLoop    = T2_LOOP_V;
#if BZ_FAST
        if (endKnown <= 0.0f) vLoop = T2B_LOOP_V_BLIND;
#endif
        if (!alongFace(tl, fmaxf(T2_LANE_CAP, fabsf(gapLane)), 1, vLoop, endKnown)) return 0;
    }
#if BZ_FLY
    Motion_Fly(BZ_FLY_ALL, 1);                           // loop turn 2 rolls straight on into the return
#endif                                                   //   (Banzai 3: rolls in from the SE behind too)
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
#if BZ_FLY
    const float swing = T2B_FLY_SWING_CM + ((s > 0) ? T2B_ENTRY_LATE_L : 0.0f);  // 15 Oct: left entry turn bites ~40 cm late
#else
    const float swing = 0.0f;
#endif
    while (x0 + swing > x - T2_TURN_LEAD && al < T2_ALPHA_MAX) { // no room for the turn: steepen
        al += 0.5f;
        entryPlan(al, fabsf(y), R, ya, &x0, &Lh);
    }
#if T2_OB1_GUARD
    // 14 Oct: keep the planned path T2_OB1_MIN_CM clear of obstacle 1 (seen at scan 1).
    // At d1 = 60 alpha 20 passes it with 9.8 cm and the robot ran ~7 cm closer than
    // planned; a steeper entry turns later and passes it farther out.
    while (x1f > 0.0f && al < T2B_ALPHA_MAX &&
           entryOb1Clear(x, y, R, s, al, x0, Lh) < T2_OB1_MIN_CM) {
        al += 1.0f;
        entryPlan(al, fabsf(y), R, ya, &x0, &Lh);
    }
#endif
    Motion_EndSteer(s > 0);                              // step 9: the entry turn next
#if BZ_FLY && T2B_FLY_ENTRY
    // Banzai "sweeping return": return straight -> entry turn -> entry straight
    // without stopping. A flying turn bites later (the servo is still swinging),
    // so it starts T2B_FLY_SWING_CM early; it lets go of the lock early by the
    // yaw the robot still makes while the wheels straighten (T2B_FLY_LEAD_S).
    x0 += swing;
    Motion_Fly(1, 1);
#elif BZ_FLY
    Motion_Fly(1, 0);                                    // rolls in from loop turn 2, stops before the entry turn
#endif
    if (!fastStraight(x - x0)) return 0;                 // heading 180: forward = toward -x
#if BZ_FLY && T2B_FLY_ENTRY
    Motion_Fly(1, 0);
#endif
    if (!turn(s > 0, al)) return 0;

#if BZ_FLY && T2B_FLY_ENTRY
    {   // Re-plan the entry from where the robot really is (a flown turn ends a
        // little off the planned point): distance along the heading to the parked point.
        float px, py, ph;
        Motion_GetPose(&px, &py, &ph);
        float a  = al * DEG2RAD;
        float xa = -(T2_M_IN + T2_REAR_CM * cosf(a) + T2_HALFW_CM * sinf(a));
        float hT  = 180.0f * roundf(h / 180.0f) + (float)s * al;   // entry turn's target heading (unwrapped)
		float brg = atan2f(-T2_ENTRY_DRIFT_CM - py, xa - px) / DEG2RAD;
		float d   = brg - hT;
		d -= 360.0f * roundf(d / 360.0f);                           // + = more to the left
		if (fabsf(d) >= T2_AIM_FIX_DEG) {                           // 15 Oct: re-aim at the parked point
			d = fminf(fmaxf(d, -T2_AIM_FIX_MAX), T2_AIM_FIX_MAX);
			if (!turn(d > 0.0f, fabsf(d))) return 0;
			Motion_GetPose(&px, &py, &ph);
		}
        Lh = (xa - px) * cosf(ph * DEG2RAD) + (-T2_ENTRY_DRIFT_CM - py) * sinf(ph * DEG2RAD);
    }
#endif
    Until_t o;
    Motion_UntilInit(&o);
    o.maxCm      = Lh;                                   // stop on tracked distance
    o.vCap       = T2_FAST_V;
    o.slowZoneCm = T2_ENTRY_SLOW_CM;                     // step 9: last 30 cm at 40 cm/s (was 45 at 25)
    o.vSlow      = T2_ENTRY_V;
#if BZ_PUNCH
    o.slowZoneCm = 0.0f;                                 // Banzai 3: straight in at T2_FAST_V, the 150 cm/s^2
#endif                                                   //   slow-down profile stops it at the parked point
#if BZ_ENTRY_BRAKE
    o.vCap       = T2B_ENTRY_V;                          // Banzai: no slow zone; hard brake T2B_BRAKE_CM early, skid in
    o.slowZoneCm = 0.0f;
    o.brakeAtCm  = T2B_BRAKE_CM;
#endif
#if BZ_FLY && T2B_FLY_ENTRY
    Motion_Fly(1, 0);
#endif
#if 1
    o.guardSide  = (s > 0) ? IR_LEFT : IR_RIGHT;         // far-side IR: the other arm
    o.guardCm    = T2_GUARD_CM;
    o.guardFromCm = fmaxf(0.0f, Lh - T2_GUARD_FROM_CM);  // only near the arms (it would see obstacle 2 earlier)
    o.usEmergCm  = T2_US_EMERG_CM;
#endif                                                   // Banzai 3: no IR guard / ultrasonic emergency stop
    return sensorStraight(&o, "ENTRY");
}

void Task2_Arrow(int left, char *reply, size_t len)
{
    int a = left ? 1 : -1;
    if (phase == 1 || phase == 2) trAdd(left ? "AL" : "AR", 0, 0);   // when the Pi's arrow arrived
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
        trAdd("FIN", 0, 0);
        float x, y, h;
        Motion_GetPose(&x, &y, &h);
        MoveResult_t r;
        Motion_LastResult(&r);
        snprintf(reply, len, "FIN x=%.1f y=%.1f h=%+.1f by=%c end=%.1f re=%d", x, y, h, r.stopBy, endFound, reTries);
    } else {
        snprintf(reply, len, "T2 ERR phase %d", phase);
    }
}

/* ---- GL / GR (Banzai 2): GO with arrow 1 already known (the Pi read it from the
 * carpark). No stop at scan 1: the approach hands over at speed into the dodge,
 * and the reply is scan 2's: "S2 u= w= by= a= u1=<scan 1 echo>". Then AL / AR
 * for arrow 2 as usual. GO / AL / AR still work as before. */
void Task2_GoArrow(int left, char *reply, size_t len)
{
    if (phase != 0) {
        snprintf(reply, len, "S2 ERR phase %d, send ZH", phase);
        return;
    }
    a1 = left ? 1 : -1;
    trStart();
    trAdd(left ? "GL" : "GR", 0, 0);
    MoveResult_t r1, r;
    if (!goMoves(&r1, 1)) {
        snprintf(reply, len, "S2 ABORT at %s", failAt);
        return;
    }
    phase = 1;
    goFly = 1;
    int ok = arrow1Moves(&r);
    goFly = 0;
    if (!ok) {
        phase = 0;
        snprintf(reply, len, "S2 ABORT at %s", failAt);
        return;
    }
    phase = 2;
    snprintf(reply, len, "S2 u=%.1f w=%.1f by=%c a=%.1f u1=%.1f", r.u, wallY, r.stopBy, aimDeg, r1.u);
}

/* ---- Bench: one SE leg with the default lane cap. Reply: Motion_Reply() ----- */
void Task2_AlongFace(int turnLeft)
{
    alongFace(turnLeft, T2_LANE_CAP, 1, FACE_V_MAX, -1.0f);
}
