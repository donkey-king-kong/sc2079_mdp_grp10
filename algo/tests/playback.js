/*
 * Simulator playback test.  node tests/playback.js
 *
 * Optional, and deliberately not wired into run_tests.py: it needs Node, and
 * the Python suite is meant to run on a bare interpreter. Run it after touching
 * anything in static/index.html.
 *
 * It exists because the animation fails *silently*. The first version of tick()
 * spent a per-frame "budget" in whole steps and discarded the remainder, so
 * with one frame at 60fps worth 17ms of simulated time and a 3cm sample worth
 * 75ms, no frame ever covered a whole step: the robot sat at the start line
 * forever with no error anywhere. Nothing in the Python tests could see that,
 * and neither could a glance at the code.
 *
 * So this stubs out just enough DOM and canvas to load the real page script,
 * pumps synthetic 60fps frames through the real tick(), and checks the robot
 * actually travels the planned path and stops on the last capture pose.
 *
 * Fixtures come from the running server, so start it first:
 *     python3 server.py &
 *     node tests/playback.js
 */

"use strict";
const fs = require("fs");
const path = require("path");
const http = require("http");

const PORT = process.env.PORT || 5001;
const ROOT = path.join(__dirname, "..");

// --- a DOM barely large enough to load the page script ---------------------

const drawCalls = [];
const ctx = new Proxy({}, {
  get: (_, name) => (...args) => { drawCalls.push(String(name)); },
  set: () => true,
});

function makeElement(id) {
  const listeners = {};
  return {
    id,
    checked: id === "show-trail",
    value: id === "speed" ? "1.5" : "exhaustive",
    textContent: "", innerHTML: "", hidden: false, disabled: false, className: "",
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    style: {},
    addEventListener(kind, fn) { (listeners[kind] = listeners[kind] || []).push(fn); },
    fire(kind, event) { (listeners[kind] || []).forEach((fn) => fn(event)); },
    setPointerCapture() {}, scrollIntoView() {}, appendChild() {},
    querySelectorAll: () => Object.assign([], { forEach: Array.prototype.forEach }),
    getContext: () => ctx,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 760, height: 760 }),
    width: 760, height: 760,
    getAttribute(name) { return this[name] || null; },
  };
}

const elements = {};
elements.arena = makeElement("arena");
const driveButtons = [];      // filled from the page's own [data-drive] markup
global.document = {
  documentElement: {},
  getElementById: (id) => (elements[id] = elements[id] || makeElement(id)),
  createElement: () => makeElement("generated"),
  querySelectorAll: (selector) => {
    const found = selector === "[data-drive]" ? driveButtons : [];
    return Object.assign(found.slice(), { forEach: Array.prototype.forEach });
  },
};
global.getComputedStyle = () => ({ getPropertyValue: () => "#123456" });
global.requestAnimationFrame = () => 0;      // frames are pumped by hand below

// --- helpers ---------------------------------------------------------------

function request(route, body) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: "localhost", port: PORT, path: route, method: body ? "POST" : "GET",
        headers: body ? { "Content-Type": "application/json",
                          "Content-Length": Buffer.byteLength(payload) } : {} },
      (res) => {
        let text = "";
        res.on("data", (c) => (text += c));
        res.on("end", () => { try { resolve(JSON.parse(text)); } catch (e) { reject(e); } });
      });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

const failures = [];
function check(label, condition, detail) {
  console.log((condition ? "  ok   " : "  FAIL ") + label + (detail ? "  -- " + detail : ""));
  if (!condition) failures.push(label);
}

// --- the test --------------------------------------------------------------

(async () => {
  let fixtures;
  try {
    const config = await request("/api/config");
    const layout = (await request("/api/random")).obstacles;
    const obstacles = layout.map((o) => ({ id: o.id, x: o.x, y: o.y, face: o.face }));
    const plan = await request("/api/plan",
      { obstacles, units: "cell", strategy: "exhaustive", metric: "time" });
    const compare = await request("/api/compare", { obstacles, units: "cell", metric: "time" });
    fixtures = { config, random: { obstacles: layout }, plan, compare };
  } catch (err) {
    console.error("Could not reach the server on port " + PORT +
                  " -- start it with `python3 server.py` first.\n" + err.message);
    process.exit(2);
  }

  global.fetch = async (route, init) => {
    // Manual driving goes to the real server: the point of those buttons is
    // that they drive the robot through the actual command parser and the
    // actual collision check, so stubbing them out would test nothing.
    if (route === "/api/drive") {
      const answer = await request(route, JSON.parse(init.body));
      return { ok: true, json: async () => answer };
    }
    return { ok: true, json: async () => (route === "/api/config" ? fixtures.config
      : route === "/api/random" ? fixtures.random
      : route === "/api/plan" ? fixtures.plan : fixtures.compare) };
  };

  // Load the real page script and expose the internals we need to drive.
  const html = fs.readFileSync(path.join(ROOT, "static", "index.html"), "utf8");
  for (const m of html.matchAll(/data-drive="([^"]+)"/g)) {
    const button = makeElement("drive-" + m[1]);
    button["data-drive"] = m[1];
    driveButtons.push(button);
  }
  const script = html.match(/<script>\n([\s\S]*?)\n<\/script>/)[1];
  eval(script + "\nglobal.__ui = { tick, draw, buildTimeline, showResult," +
       " renderComparison, displayPose, currentLeg, steps: () => steps," +
       " cursor: () => cursor, clock: () => clock, isPlaying: () => playing," +
       " setPlan: (p) => { plan = p; }, setObstacles: (o) => { obstacles = o; }," +
       " setManualPose: (p) => { manualPose = p; manualTrail = []; } };");
  await new Promise((r) => setTimeout(r, 30));       // let the start() IIFE settle

  const ui = global.__ui;
  const plan = fixtures.plan;
  ui.setObstacles(fixtures.random.obstacles.map((o) => ({ id: o.id, x: o.x, y: o.y, face: o.face })));
  ui.setPlan(plan);
  ui.buildTimeline();
  ui.showResult(plan);

  console.log("Simulator playback (" + plan.order.length + " obstacles, " +
              plan.total_duration + "s run, " + plan.total_distance + "cm)\n");

  // 1. Rendering.
  drawCalls.length = 0;
  elements["show-virtual"].checked = true;
  elements["show-poses"].checked = true;
  ui.draw();
  check("draw() renders without throwing", drawCalls.length > 100, drawCalls.length + " canvas ops");
  ui.renderComparison(fixtures.compare);
  check("comparison view renders", true);

  // 2. The timeline agrees with the planner.
  const steps = ui.steps();
  const finalT = steps[steps.length - 1].t;
  check("timeline ends at the reported total",
        Math.abs(finalT - plan.total_duration) < 0.05,
        finalT.toFixed(2) + "s vs " + plan.total_duration + "s");

  // 3. Playback actually moves the robot. This is the regression guard.
  for (const speed of [0.25, 1.5, 6]) {
    elements.speed.value = String(speed);
    elements["btn-reset"].fire("click", {});
    elements["btn-play"].fire("click", {});

    let now = 0, travelled = 0, moved = 0;
    let previous = ui.displayPose();
    const limit = Math.ceil(plan.total_duration / speed * 60) + 300;
    let frames = 0;
    while (ui.isPlaying() && frames < limit) {
      now += 1000 / 60;
      frames++;
      ui.tick(now);
      const pose = ui.displayPose();
      const step = Math.hypot(pose.x - previous.x, pose.y - previous.y);
      travelled += step;
      if (step > 1e-9) moved++;
      previous = pose;
    }

    const goal = plan.legs[plan.legs.length - 1].end;
    const here = ui.displayPose();
    check("x" + speed + " robot drives the whole path",
          travelled > plan.total_distance * 0.95,
          travelled.toFixed(1) + "cm of " + plan.total_distance + "cm");
    check("x" + speed + " robot stops on the last capture pose",
          Math.hypot(here.x - goal.x, here.y - goal.y) < 1.0,
          "(" + here.x.toFixed(1) + ", " + here.y.toFixed(1) + ")");
    check("x" + speed + " playback stops itself", !ui.isPlaying());
    check("x" + speed + " some frames are held still for the photos",
          moved < frames, (frames - moved) + " held of " + frames + " frames");
  }

  // 4. Step and Reset.
  elements.speed.value = "1.5";
  elements["btn-reset"].fire("click", {});
  let stepsOk = true;
  for (let i = 1; i <= plan.legs.length; i++) {
    elements["btn-step"].fire("click", {});
    if (ui.currentLeg() !== i) stepsOk = false;
    if (Math.abs(ui.clock() - steps[ui.cursor()].t) > 1e-9) stepsOk = false;
  }
  check("Step advances one obstacle at a time, clock in sync", stepsOk);

  elements["btn-reset"].fire("click", {});
  const home = ui.displayPose();
  check("Reset returns the robot to the start",
        ui.cursor() === 0 && ui.clock() === 0 &&
        Math.abs(home.x - plan.start.x) < 1e-9 && Math.abs(home.y - plan.start.y) < 1e-9);

  // 5. Pause must hold, and resuming must not fast-forward the elapsed real time.
  elements["btn-play"].fire("click", {});
  let t = 0;
  for (let i = 0; i < 300; i++) { t += 1000 / 60; ui.tick(t); }
  const paused = ui.clock();
  elements["btn-play"].fire("click", {});
  for (let i = 0; i < 120; i++) { t += 1000 / 60; ui.tick(t); }
  check("Pause holds the clock", Math.abs(ui.clock() - paused) < 1e-9);
  elements["btn-play"].fire("click", {});
  t += 5000;                                  // pretend the tab was hidden for 5s
  ui.tick(t);
  check("Resume does not jump after a real-time gap",
        ui.clock() - paused < 0.1, "advanced " + (ui.clock() - paused).toFixed(3) + "s");

  // 6. The live count of images recognised (checklist B.2 is scored on it).
  elements["btn-reset"].fire("click", {});
  const atRest = elements["v-images"].textContent;
  elements["btn-play"].fire("click", {});
  let counted = true, seen = [];
  let u = 0;
  while (ui.isPlaying() && u < 100000) {
    u += 1000 / 60;
    ui.tick(u);
    const shown = parseInt(elements["v-images"].textContent, 10);
    if (shown !== ui.currentLeg()) counted = false;
    if (!seen.includes(shown)) seen.push(shown);
  }
  check("images-recognised counter starts at zero", atRest.startsWith("0"), "showed " + atRest);
  check("images-recognised counter tracks the run", counted,
        "reached " + seen[seen.length - 1] + " of " + plan.order.length);

  // 7. Manual drive: forward, backward and turning on demand (checklist B.1).
  //
  // Each command is measured from the same open starting pose, because the
  // moves are not independent -- driving the six in sequence walks the robot
  // into a wall, and the refusal that follows is correct behaviour, not a
  // failure. Obstacles are cleared so only the walls constrain; the refusal
  // path gets its own check below.
  elements["btn-reset"].fire("click", {});
  const startLine = ui.displayPose();
  const middle = { x: 100, y: 100, theta: 0 };      // mid-arena, facing East
  ui.setObstacles([]);

  const moves = [];
  for (const button of driveButtons) {
    ui.setManualPose(Object.assign({}, middle));
    await button.fire("click", {});
    await new Promise((r) => setTimeout(r, 40));
    const after = ui.displayPose();
    moves.push({
      command: button["data-drive"],
      shifted: Math.hypot(after.x - middle.x, after.y - middle.y),
      turned: Math.abs(after.theta - middle.theta) * 180 / Math.PI,
    });
  }

  const straight = moves.filter((m) => m.command[0] === "S");
  const turning = moves.filter((m) => m.command[0] !== "S");
  check("manual drive moves the robot forward and backward",
        straight.length === 2 && straight.every((m) => m.shifted > 9 && m.turned < 0.01),
        straight.map((m) => m.command + " " + m.shifted.toFixed(1) + "cm").join(", "));
  check("manual drive turns the robot, forwards and in reverse",
        turning.length === 4 && turning.every((m) => Math.abs(m.turned - 30) < 0.01 && m.shifted > 1),
        turning.map((m) => m.command + " " + m.turned.toFixed(0) + "deg/" + m.shifted.toFixed(0) + "cm").join(", "));

  // Reversing on left lock swings the nose the opposite way to driving forward
  // on left lock. Getting this backwards would have the robot mirror itself.
  const lf = moves.find((m) => m.command === "LF030");
  const lb = moves.find((m) => m.command === "LB030");
  ui.setManualPose(Object.assign({}, middle));
  await driveButtons.find((b) => b["data-drive"] === "LF030").fire("click", {});
  await new Promise((r) => setTimeout(r, 40));
  const afterLF = ui.displayPose().theta;
  ui.setManualPose(Object.assign({}, middle));
  await driveButtons.find((b) => b["data-drive"] === "LB030").fire("click", {});
  await new Promise((r) => setTimeout(r, 40));
  const afterLB = ui.displayPose().theta;
  check("reverse-left swings the nose opposite to forward-left",
        Math.sign(afterLF - middle.theta) === -Math.sign(afterLB - middle.theta),
        "LF " + (afterLF * 180 / Math.PI).toFixed(0) + "deg, LB " + (afterLB * 180 / Math.PI).toFixed(0) + "deg");

  // A move into a wall must be refused, not driven.
  ui.setManualPose({ x: 100, y: 180, theta: Math.PI / 2 });   // facing the top wall
  const wall = ui.displayPose();
  await driveButtons.find((b) => b["data-drive"] === "SF010").fire("click", {});
  await new Promise((r) => setTimeout(r, 40));
  const held = ui.displayPose();
  check("a move that would leave the arena is refused",
        Math.abs(held.y - wall.y) < 1e-9, "robot held at y=" + held.y.toFixed(1));

  ui.setObstacles(fixtures.random.obstacles.map((o) => ({ id: o.id, x: o.x, y: o.y, face: o.face })));
  elements["btn-home"].fire("click", {});
  const back = ui.displayPose();
  check("\"Back to start\" returns the robot to the start zone",
        Math.abs(back.x - startLine.x) < 1e-9 && Math.abs(back.y - startLine.y) < 1e-9);

  console.log(failures.length ? "\nFAILED: " + failures.join("; ") : "\nAll playback checks passed");
  process.exit(failures.length ? 1 : 0);
})().catch((err) => { console.error(err.stack || err); process.exit(1); });
