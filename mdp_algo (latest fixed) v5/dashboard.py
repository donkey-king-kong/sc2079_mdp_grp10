import matplotlib.pyplot as plt
import matplotlib.patches as patches
import matplotlib.widgets as widgets
import matplotlib.transforms as transforms
import requests
import numpy as np

# CONFIGURATION
API_URL = "http://localhost:5000"
GRID_SIZE = 20

# Direction labels for tooltip
ANDROID_DIR = {1: "N", 2: "E", 3: "S", 4: "W"}   # Android: 1=N, 2=E, 3=S, 4=W
ALGO_DIR = {0: "N", 2: "E", 4: "S", 6: "W"}       # Algo: 0=N, 2=E, 4=S, 6=W
ANDROID_TO_ALGO = {1: 0, 2: 2, 3: 4, 4: 6}

class InteractiveDashboard:
    def __init__(self):
        self.obstacles = []
        self.raw_path = []
        self.interpolated_path = []
        self.visited_ids = []
        self.bullseye_history = {} 
        self.cost = 0
        self.current_frame = 0
        self.is_playing = False
        
        self.fig, self.ax = plt.subplots(figsize=(10, 11))
        plt.subplots_adjust(bottom=0.2)
        
        self.timer = self.fig.canvas.new_timer(interval=50)
        self.timer.add_callback(self.play_step)
        
        self.btn_prev = widgets.Button(plt.axes([0.05, 0.05, 0.12, 0.075]), '<< Prev')
        self.btn_prev.on_clicked(self.prev_step)
        
        self.btn_play = widgets.Button(plt.axes([0.19, 0.05, 0.12, 0.075]), 'Play', color='lightgreen')
        self.btn_play.on_clicked(self.toggle_play)
        
        self.btn_next = widgets.Button(plt.axes([0.33, 0.05, 0.12, 0.075]), 'Next >>')
        self.btn_next.on_clicked(self.next_step)

        self.btn_run = widgets.Button(plt.axes([0.55, 0.05, 0.18, 0.075]), 'Calculate Path', color='lightblue')
        self.btn_run.on_clicked(self.run_simulation)
        
        self.btn_clear = widgets.Button(plt.axes([0.75, 0.05, 0.12, 0.075]), 'Clear', color='salmon')
        self.btn_clear.on_clicked(self.clear_all)

        self.cid = self.fig.canvas.mpl_connect('button_press_event', self.on_click)
        self.hover_annot = None
        self.fig.canvas.mpl_connect('motion_notify_event', self.on_hover)
        
        self.redraw()
        plt.show()

    def on_hover(self, event):
        if self.hover_annot is None:
            return
        if event.inaxes != self.ax or event.xdata is None or event.ydata is None:
            self.hover_annot.set_visible(False)
            self.fig.canvas.draw_idle()
            return
        ix, iy = int(event.xdata), int(event.ydata)
        obs = next((o for o in self.obstacles if o["x"] == ix and o["y"] == iy), None)
        if obs is None:
            self.hover_annot.set_visible(False)
        else:
            d_android = obs["d"]
            d_algo = ANDROID_TO_ALGO.get(d_android, 0)
            label_android = ANDROID_DIR.get(d_android, "?")
            label_algo = ALGO_DIR.get(d_algo, "?")
            self.hover_annot.set_text(
                f"Obstacle #{obs['id']}  at ({obs['x']}, {obs['y']})\n"
                f"Android: d={d_android} ({label_android})  →  Algo: d={d_algo} ({label_algo})"
            )
            self.hover_annot.xy = (ix + 0.5, iy + 0.5)
            self.hover_annot.set_visible(True)
        self.fig.canvas.draw_idle()

    def generate_smooth_path(self, raw_path):
        frames = []
        if not raw_path: return frames

        for i in range(len(raw_path) - 1):
            p1, p2 = raw_path[i], raw_path[i+1]
            d1_deg = {0: 90, 2: 0, 4: -90, 6: 180}[p1['d']]
            dx, dy = p2['x'] - p1['x'], p2['y'] - p1['y']
            
            hx, hy = {0:(0,1), 2:(1,0), 4:(0,-1), 6:(-1,0)}[p1['d']]
            is_reverse = (dx*hx + dy*hy) < -0.1

            if p1['s'] != -1:
                for _ in range(5): frames.append({'x': p1['x'], 'y': p1['y'], 'angle': d1_deg, 's': p1['s']})

            if p1['d'] == p2['d']: # Straight
                steps = max(1, int(np.hypot(dx, dy) * 2))
                for t in np.linspace(0, 1, steps, endpoint=False):
                    frames.append({'x': p1['x'] + dx*t, 'y': p1['y'] + dy*t, 'angle': d1_deg, 's': -1})
            else: # Turn
                r = 4  
                grid_jump = 4  
                
                if p1['d'] == 0:   pivot = (p1['x'] - grid_jump, p1['y']) if dx < 0 else (p1['x'] + grid_jump, p1['y'])
                elif p1['d'] == 4: pivot = (p1['x'] + grid_jump, p1['y']) if dx > 0 else (p1['x'] - grid_jump, p1['y'])
                elif p1['d'] == 2: pivot = (p1['x'], p1['y'] + grid_jump) if dy > 0 else (p1['x'], p1['y'] - grid_jump)
                elif p1['d'] == 6: pivot = (p1['x'], p1['y'] - grid_jump) if dy < 0 else (p1['x'], p1['y'] + grid_jump)

                start_vec = np.array([p1['x'] - pivot[0], p1['y'] - pivot[1]])
                end_vec   = np.array([p2['x'] - pivot[0], p2['y'] - pivot[1]])
                angle_start = np.arctan2(start_vec[1], start_vec[0])
                angle_end   = np.arctan2(end_vec[1], end_vec[0])

                if abs(angle_end - angle_start) > np.pi:
                    if angle_end > angle_start: angle_start += 2*np.pi
                    else: angle_end += 2*np.pi

                is_ccw = angle_end > angle_start
                steps = 15
                for t in np.linspace(0, 1, steps, endpoint=False):
                    theta = angle_start + (angle_end - angle_start) * t
                    cx, cy = pivot[0] + r*np.cos(theta), pivot[1] + r*np.sin(theta)
                    tangent = theta + (np.pi/2 if is_ccw else -np.pi/2)
                    if is_reverse: tangent += np.pi
                    frames.append({'x': cx, 'y': cy, 'angle': np.degrees(tangent), 's': -1})

        last = raw_path[-1]
        ld = {0: 90, 2: 0, 4: -90, 6: 180}[last['d']]
        frames.append({'x': last['x'], 'y': last['y'], 'angle': ld, 's': last['s']})
        return frames

    def toggle_play(self, event):
        if not self.interpolated_path: return
        if self.is_playing: self.stop_playback()
        else:
            if self.current_frame >= len(self.interpolated_path) - 1: self.current_frame = 0
            self.is_playing = True
            self.btn_play.label.set_text('Pause'); self.btn_play.color = 'yellow'
            self.timer.start()

    def stop_playback(self):
        self.is_playing = False
        self.btn_play.label.set_text('Play'); self.btn_play.color = 'lightgreen'
        self.timer.stop()
        self.fig.canvas.draw_idle()

    def play_step(self):
        if self.current_frame < len(self.interpolated_path) - 1:
            self.current_frame += 1; self.redraw()
        else: self.stop_playback()

    def prev_step(self, event):
        self.stop_playback()
        if self.current_frame > 0: self.current_frame -= 1; self.redraw()

    def next_step(self, event):
        self.stop_playback()
        if self.interpolated_path and self.current_frame < len(self.interpolated_path) - 1:
            self.current_frame += 1; self.redraw()

    def on_click(self, event):
        if event.inaxes != self.ax: return
        x, y = int(event.xdata), int(event.ydata)
        if not (0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE): return
        if x < 4 and y < 4: return 

        existing_idx = next((i for i, o in enumerate(self.obstacles) if o['x'] == x and o['y'] == y), -1)
        
        if event.button == 3: 
            if existing_idx != -1: self.obstacles.pop(existing_idx); self.reset_path()
        if event.button == 2: 
            if existing_idx != -1: self.simulate_bullseye(self.obstacles[existing_idx]['id'])
        if event.button == 1: 
            if existing_idx != -1: 
                # Android format rotation: 1(N)->2(E)->3(S)->4(W)->1(N)
                self.obstacles[existing_idx]['d'] = (self.obstacles[existing_idx]['d'] % 4) + 1
            else:
                if len(self.obstacles) >= 8: return
                self.obstacles.append({"id": len(self.obstacles) + 1, "x": x, "y": y, "d": 1}) # Default to 1 (North)
            self.reset_path()

    def simulate_bullseye(self, target_id):
        if not self.interpolated_path: return
        self.stop_playback()
        frame = self.interpolated_path[self.current_frame]

        if frame['s'] == -1:
            warning_text = "❌ INVALID TIMING: Wait for the robot to STOP (📸) before checking!"
            self.ax.set_title(warning_text, color='red', fontweight='bold')
            self.fig.canvas.draw()
            return

        a = frame['angle'] % 360
        if 45 <= a < 135: r_dir_algo = 0
        elif 135 <= a < 225: r_dir_algo = 6
        elif 225 <= a < 315: r_dir_algo = 4
        else: r_dir_algo = 2

        algo_to_android = {0: 1, 2: 2, 4: 3, 6: 4}
        currently_checking_algo = (r_dir_algo + 4) % 8
        currently_checking_face = algo_to_android[currently_checking_algo]
        
        if target_id not in self.bullseye_history: self.bullseye_history[target_id] = []
        if currently_checking_face not in self.bullseye_history[target_id]:
            self.bullseye_history[target_id].append(currently_checking_face)
            
        remaining_sides = 4 - len(self.bullseye_history[target_id])

        if remaining_sides > 0:
            warning_text = f"🎯 BULLSEYE ON OBS #{target_id}!\nProcessing... Searching {remaining_sides} remaining sides."
        else:
            warning_text = f"💀 ALL SIDES CHECKED ON OBS #{target_id}!\nGiving up and routing to next obstacle."
            
        self.ax.set_title(warning_text, color='darkred', fontweight='bold', fontsize=12)
        self.fig.canvas.draw()
        plt.pause(0.5) 

        # Translate obstacles and checked faces before sending to Python server
        algo_obstacles = []
        for obs in self.obstacles:
            algo_dir = ANDROID_TO_ALGO.get(obs['d'], 0)
            algo_obstacles.append({
                "id": obs['id'], 
                "x": obs['x'], 
                "y": obs['y'], 
                "d": algo_dir
            })

        algo_checked_faces = [ANDROID_TO_ALGO.get(f, 0) for f in self.bullseye_history[target_id]]

        payload = {
            "obstacles": algo_obstacles,
            "robot_x": int(round(frame['x'])),
            "robot_y": int(round(frame['y'])),
            "robot_dir": r_dir_algo, 
            "target_obstacle_id": target_id,
            "checked_faces": algo_checked_faces 
        }

        try:
            res = requests.post(f"{API_URL}/bullseye", json=payload)
            if res.status_code == 200:
                data = res.json()
                self.raw_path = data['data']['path']
                self.cost = data['data']['distance']
                self.interpolated_path = self.generate_smooth_path(self.raw_path)
                
                self.visited_ids = []
                for p in self.raw_path:
                    if p['s'] != -1 and p['s'] not in self.visited_ids: self.visited_ids.append(p['s'])

                self.current_frame = 0; self.redraw()
            else: 
                self.ax.set_title(f"❌ Server Error: Could not route around #{target_id}", color='red')
                self.fig.canvas.draw()
        except Exception as e: print(f"❌ Connection Failed: {e}")

    def reset_path(self):
        self.stop_playback()
        for i, obs in enumerate(self.obstacles): obs['id'] = i + 1
        self.raw_path = []; self.interpolated_path = []; self.visited_ids = []
        self.bullseye_history = {} 
        self.cost = 0; self.current_frame = 0
        self.redraw()

    def run_simulation(self, event):
        if not self.obstacles: return
        print(f"📡 Sending {len(self.obstacles)} obstacles...")
        
        # Translate obstacles to Algo format before sending
        algo_obstacles = []
        for obs in self.obstacles:
            algo_dir = ANDROID_TO_ALGO.get(obs['d'], 0)
            algo_obstacles.append({
                "id": obs['id'], 
                "x": obs['x'], 
                "y": obs['y'], 
                "d": algo_dir
            })
            
        try:
            res = requests.post(f"{API_URL}/path", json={"obstacles": algo_obstacles, "robot_x": 1, "robot_y": 1, "robot_dir": 0})
            if res.status_code == 200:
                data = res.json()
                self.raw_path = data['data']['path']
                self.cost = data['data']['distance']
                self.interpolated_path = self.generate_smooth_path(self.raw_path)
                
                self.visited_ids = []
                for p in self.raw_path:
                    if p['s'] != -1 and p['s'] not in self.visited_ids: self.visited_ids.append(p['s'])

                self.current_frame = 0; self.stop_playback(); self.redraw()
            else: print(f"❌ Server Error: {res.text}")
        except Exception as e: print(f"❌ Connection Failed: {e}")

    def clear_all(self, event): self.obstacles = []; self.reset_path()

    def redraw(self):
        self.ax.clear()
        self.ax.set_xlim(0, 20); self.ax.set_ylim(0, 20)
        self.ax.set_xticks(range(21)); self.ax.set_yticks(range(21))
        self.ax.grid(True, linestyle=':', alpha=0.6)
        
        status = "Setup Mode"
        if self.interpolated_path: status = f"Frame {self.current_frame}/{len(self.interpolated_path)-1}"
        
        visited_count = len(self.visited_ids)
        total_count = len(self.obstacles)
        title_str = f"{status}\nObstacles: {total_count} | Cost: {self.cost:.2f}"
        if self.raw_path:
            title_str += f"\n✅ Visited: {visited_count}/{total_count} {self.visited_ids}"
            
        self.ax.set_title(title_str, fontsize=10)
        self.ax.add_patch(patches.Rectangle((0, 0), 4, 4, color='lightgreen', alpha=0.4))
        self.ax.text(2, 2, "START", ha='center', va='center', color='green', fontweight='bold')

        for obs in self.obstacles:
            x, y, d = obs['x'], obs['y'], obs['d']
            self.ax.add_patch(patches.Rectangle((x, y), 1, 1, color='salmon', ec='darkred', zorder=2))
            
            face_map = {1:(0.5,1,0,0.3), 2:(1,0.5,0.3,0), 3:(0.5,0,0,-0.3), 4:(0,0.5,-0.3,0)}
            ox, oy, dx, dy = face_map[d]
            self.ax.arrow(x+ox, y+oy, dx, dy, color='green', width=0.08, head_width=0.25, zorder=3)
            self.ax.text(x+0.5, y+0.5, str(obs['id']), ha='center', va='center', color='white', fontweight='bold')
            dir_label = f"Android d={d} ({ANDROID_DIR.get(d,'?')})  Algo d={ANDROID_TO_ALGO.get(d,0)} ({ALGO_DIR.get(ANDROID_TO_ALGO.get(d,0),'?')})"
            self.ax.text(x+0.5, y+1.02, dir_label, ha='center', va='bottom', fontsize=5, color='darkred')

        if self.raw_path:
            for i in range(len(self.raw_path) - 1):
                p1, p2 = self.raw_path[i], self.raw_path[i+1]
                x1, y1 = p1['x'] + 0.5, p1['y'] + 0.5
                x2, y2 = p2['x'] + 0.5, p2['y'] + 0.5
                
                color = plt.cm.winter(i / len(self.raw_path))
                if p1['d'] != p2['d']:
                    diff = (p2['d'] - p1['d']) % 8
                    rad = -0.3 if diff == 2 else 0.3
                    arrow = patches.FancyArrowPatch((x1, y1), (x2, y2), connectionstyle=f"arc3,rad={rad}", color=color, alpha=0.3, arrowstyle='-', linewidth=2, zorder=1)
                    self.ax.add_patch(arrow)
                else: self.ax.plot([x1, x2], [y1, y2], color=color, alpha=0.3, linewidth=2, zorder=1)

            visit_order = 1; visited_set = set()
            for i, p in enumerate(self.raw_path):
                if p['s'] != -1 or (i < len(self.raw_path)-1 and p['d'] != self.raw_path[i+1]['d']) or i==0:
                    alpha = 0.3 if p['s'] != -1 else 0.05
                    style = '-' if p['s'] != -1 else '--'
                    
                    rx, ry = p['x'] - 1.0, p['y'] - 1.0
                    self.ax.add_patch(patches.Rectangle((rx, ry), 3, 3, color='gray', alpha=alpha, linestyle=style, zorder=1))
                    
                    cx, cy, cd = p['x'] + 0.5, p['y'] + 0.5, p['d']
                    if cd == 0: rect = (cx-0.5, cy+1.4, 1, 0.2)
                    elif cd == 2: rect = (cx+1.4, cy-0.5, 0.2, 1)
                    elif cd == 4: rect = (cx-0.5, cy-1.6, 1, 0.2)
                    elif cd == 6: rect = (cx-1.6, cy-0.5, 0.2, 1)
                    self.ax.add_patch(patches.Rectangle(rect[:2], rect[2], rect[3], color='red', alpha=alpha*2, zorder=1))

                    if p['s'] != -1 and p['s'] not in visited_set:
                        target = next((o for o in self.obstacles if o['id'] == p['s']), None)
                        if target:
                            od = target['d']
                            lx, ly = target['x'] + 0.5, target['y'] + 0.5
                            if od == 1:   ly -= 0.5
                            elif od == 2: lx -= 0.5
                            elif od == 3: ly += 1.5
                            elif od == 4: lx += 1.5
                            self.ax.text(lx, ly, f"#{visit_order}", color='purple', fontsize=12, fontweight='bold', ha='center', zorder=5)
                            visited_set.add(p['s']); visit_order += 1

        if self.interpolated_path:
            f = self.interpolated_path[self.current_frame]
            fx, fy, fa = f['x'] + 0.5, f['y'] + 0.5, f['angle']
            
            t = transforms.Affine2D().rotate_deg_around(fx, fy, fa - 90) + self.ax.transData
            
            # --- 1. VIRTUAL BUFFER BOX (30x30 cm A* clearance zone) ---
            buffer_box = patches.Rectangle((fx-1.5, fy-1.5), 3, 3, color='lightgray', alpha=0.3, ec='red', linestyle='--', lw=1.5, zorder=9)
            buffer_box.set_transform(t)
            self.ax.add_patch(buffer_box)
            
            # --- 2. ACTUAL ROBOT CHASSIS (Inset slightly to 16cm so wheels protrude) ---
            chassis = patches.Rectangle((fx-0.8, fy-1.0), 1.6, 2.0, color='gray', alpha=0.8, ec='black', lw=2, zorder=10)
            chassis.set_transform(t)
            self.ax.add_patch(chassis)
            
            # --- 3. WHEELS (Outer edges exactly 20cm / 2.0 units apart) ---
            w_width, w_height = 0.4, 0.6
            w_r_l = patches.Rectangle((fx-1.0, fy-0.8), w_width, w_height, color='black', zorder=11)
            w_r_l.set_transform(t)
            self.ax.add_patch(w_r_l)
            
            w_r_r = patches.Rectangle((fx+0.6, fy-0.8), w_width, w_height, color='black', zorder=11)
            w_r_r.set_transform(t)
            self.ax.add_patch(w_r_r)
            
            w_f_l = patches.Rectangle((fx-1.0, fy+0.2), w_width, w_height, color='black', zorder=11)
            w_f_l.set_transform(t)
            self.ax.add_patch(w_f_l)
            
            w_f_r = patches.Rectangle((fx+0.6, fy+0.2), w_width, w_height, color='black', zorder=11)
            w_f_r.set_transform(t)
            self.ax.add_patch(w_f_r)

            # --- 4. CAMERA/SENSOR INDICATOR ---
            c = patches.Rectangle((fx-0.4, fy+1.0), 0.8, 0.2, color='red', zorder=11)
            c.set_transform(t)
            self.ax.add_patch(c)
            
            # --- 5. DIRECTION INDICATOR ---
            adx, ady = 0.8*np.cos(np.radians(fa)), 0.8*np.sin(np.radians(fa))
            self.ax.arrow(fx, fy, adx, ady, color='blue', width=0.1, head_width=0.3, zorder=11)
            if f['s'] != -1: self.ax.text(fx, fy, "📸", fontsize=20, ha='center', va='center', zorder=12)


        # Hover tooltip: show obstacle id, coords, Android & Algo direction
        self.hover_annot = self.ax.annotate(
            "", xy=(0, 0), fontsize=9,
            xytext=(10, 10), textcoords="offset points",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="wheat", alpha=0.9, edgecolor="gray"),
            zorder=20,
        )
        self.hover_annot.set_visible(False)

        self.fig.canvas.draw()

if __name__ == "__main__":
    dashboard = InteractiveDashboard()