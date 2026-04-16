"""
IMU STL Visualizer — reads BNO055 quaternion data from serial OR UDP and
renders a user-supplied STL file rotating in real-time to match the sensor.

Uses GLFW + PyOpenGL (no pygame).
    
Performance: mesh is uploaded to a GPU VBO once; each frame is one glDrawArrays.
Use --vsync for smooth video; leave it off for lowest motion-to-photon latency.

Usage:
    # Serial (imu_visualizer firmware):
    python imu_viewer.py COM5 --stl Paddle.stl

    # UDP (main paddle firmware: tutorial flood and gameplay use ex,ey,ez,btn,impulse):
    python imu_viewer.py --udp 4210 --stl Paddle.stl
"""

import argparse
import ctypes
import math
import select
import socket
import sys
import threading
import time
from pathlib import Path

import glfw
import numpy as np
import serial
from OpenGL.GL import *
from OpenGL.GLU import *
from stl import mesh as stl_mesh


# ── Quaternion → column-major 4×4 float32 (OpenGL) ─────────────────────────

def quat_to_gl_matrix(w, x, y, z, out: np.ndarray) -> None:
    """Write rotation matrix into out[16] column-major (float32)."""
    n = w * w + x * x + y * y + z * z
    if n < 1e-12:
        out[:] = np.eye(4, dtype=np.float32).ravel("F")
        return
    s = 2.0 / n
    wx, wy, wz = s * w * x, s * w * y, s * w * z
    xx, xy, xz = s * x * x, s * x * y, s * x * z
    yy, yz, zz = s * y * y, s * y * z, s * z * z
    r00, r01, r02 = 1 - (yy + zz), xy - wz, xz + wy
    r10, r11, r12 = xy + wz, 1 - (xx + zz), yz - wx
    r20, r21, r22 = xz - wy, yz + wx, 1 - (xx + yy)
    out[0:16] = (
        r00, r10, r20, 0.0,
        r01, r11, r21, 0.0,
        r02, r12, r22, 0.0,
        0.0, 0.0, 0.0, 1.0,
    )


# ── STL loading ─────────────────────────────────────────────────────────────

def load_stl(path: str):
    """Load an STL file, center it, and scale to fit a ±1 cube."""
    m = stl_mesh.Mesh.from_file(path)
    verts = m.vectors.reshape(-1, 3).astype(np.float32)
    normals = np.repeat(m.normals, 3, axis=0).astype(np.float32)

    center = (verts.max(axis=0) + verts.min(axis=0)) / 2
    verts -= center
    span = (verts.max(axis=0) - verts.min(axis=0)).max()
    if span > 0:
        verts *= 2.0 / span

    return verts, normals


def generate_box():
    """Fallback: a simple 3D box (paddle-ish proportions)."""
    sx, sy, sz = 0.8, 0.1, 1.2
    v = np.array([
        [-sx, -sy, -sz], [ sx, -sy, -sz], [ sx,  sy, -sz], [-sx,  sy, -sz],
        [-sx, -sy,  sz], [ sx, -sy,  sz], [ sx,  sy,  sz], [-sx,  sy,  sz],
    ], dtype=np.float32)
    faces = [
        (0, 1, 2, 3), (4, 7, 6, 5),
        (0, 3, 7, 4), (1, 5, 6, 2),
        (3, 2, 6, 7), (0, 4, 5, 1),
    ]
    normals_per_face = [
        (0, 0, -1), (0, 0, 1), (-1, 0, 0), (1, 0, 0), (0, 1, 0), (0, -1, 0),
    ]
    tri_verts = []
    tri_normals = []
    for face, n in zip(faces, normals_per_face):
        for tri in [(face[0], face[1], face[2]), (face[0], face[2], face[3])]:
            for idx in tri:
                tri_verts.append(v[idx])
                tri_normals.append(n)
    return np.array(tri_verts, dtype=np.float32), np.array(tri_normals, dtype=np.float32)


# ── GPU mesh (interleaved normal + vertex, static VBO) ─────────────────────

class StaticInterleavedMesh:
    __slots__ = ("_vbo", "_count")

    def __init__(self, vertices: np.ndarray, normals: np.ndarray):
        assert vertices.shape == normals.shape
        n = vertices.shape[0]
        self._count = n
        interleaved = np.empty((n, 6), dtype=np.float32)
        interleaved[:, :3] = normals
        interleaved[:, 3:6] = vertices
        blob = np.ascontiguousarray(interleaved)

        ids = glGenBuffers(1)
        self._vbo = int(ids[0]) if isinstance(ids, (list, tuple, np.ndarray)) else int(ids)
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo)
        glBufferData(GL_ARRAY_BUFFER, blob.nbytes, blob, GL_STATIC_DRAW)
        glBindBuffer(GL_ARRAY_BUFFER, 0)

    def draw(self) -> None:
        glColor3f(0.35, 0.65, 0.95)
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo)
        glEnableClientState(GL_NORMAL_ARRAY)
        glEnableClientState(GL_VERTEX_ARRAY)
        glNormalPointer(GL_FLOAT, 24, ctypes.c_void_p(0))
        glVertexPointer(3, GL_FLOAT, 24, ctypes.c_void_p(12))
        glDrawArrays(GL_TRIANGLES, 0, self._count)
        glDisableClientState(GL_VERTEX_ARRAY)
        glDisableClientState(GL_NORMAL_ARRAY)
        glBindBuffer(GL_ARRAY_BUFFER, 0)

    def delete(self) -> None:
        if self._vbo is not None:
            bid = np.array([self._vbo], dtype=np.uint32)
            glDeleteBuffers(1, bid)
            self._vbo = None


# ── Packet parser (shared by serial and UDP readers) ───────────────────────

def parse_q_packet(data: bytes, state_lock, state):
    """Parse packet and update shared state dict under lock."""
    parts = data.split(b",")
    if len(parts) == 13 and parts[0] == b"Q":  # old format: Q,w,x,y,z,ex,ey,ez,cal0,cal1,cal2,cal3,btn
        try:
            w = float(parts[1])
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
            euler = (0.0, 0.0, 0.0)
            cal = (0, 0, 0, 0)
            if len(parts) >= 8:
                euler = (float(parts[5]), float(parts[6]), float(parts[7]))
            if len(parts) >= 12:
                cal = (int(parts[8]), int(parts[9]), int(parts[10]), int(parts[11]))
            with state_lock:
                state["quat"][:] = (w, x, y, z)
                state["euler"][:] = euler
                state["cal"] = cal
                state["pkt_count"] += 1
        except (ValueError, IndexError):
            pass
    elif len(parts) == 5:  # new tutorial format: ex,ey,ez,btn,impulse
        try:
            ex = float(parts[0])
            ey = float(parts[1])
            ez = float(parts[2])
            btn = int(parts[3])
            impulse = float(parts[4])
            # Convert euler to quaternion (BNO055: ex=heading/yaw, ey=roll, ez=pitch)
            yaw = math.radians(ex)
            pitch = math.radians(ez)
            roll = math.radians(ey)
            cy = math.cos(yaw * 0.5)
            sy = math.sin(yaw * 0.5)
            cp = math.cos(pitch * 0.5)
            sp = math.sin(pitch * 0.5)
            cr = math.cos(roll * 0.5)
            sr = math.sin(roll * 0.5)
            w = cr * cp * cy + sr * sp * sy
            x = sr * cp * cy - cr * sp * sy
            y = cr * sp * cy + sr * cp * sy
            z = cr * cp * sy - sr * sp * cy
            with state_lock:
                state["quat"][:] = (w, x, y, z)
                state["euler"][:] = (ex, ey, ez)
                state["cal"] = (0, 0, 0, 0)  # no cal in new format
                state["pkt_count"] += 1
        except (ValueError, IndexError):
            pass
    else:
        return


# ── Serial reader thread ───────────────────────────────────────────────────

class SerialReader:
    def __init__(self, port, baud, state_lock, state):
        self.port = port
        self.baud = baud
        self._lock = state_lock
        self._state = state
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()

    def _run(self):
        while not self._stop.is_set():
            try:
                ser = serial.Serial(self.port, self.baud, timeout=0.05)
                ser.reset_input_buffer()
                with self._lock:
                    self._state["connected"] = True
                print(f"[serial] Connected to {self.port} @ {self.baud}")
            except serial.SerialException as e:
                print(f"[serial] Cannot open {self.port}: {e}. Retrying in 2s...")
                time.sleep(2)
                continue

            try:
                while not self._stop.is_set():
                    raw = ser.readline()
                    if raw:
                        parse_q_packet(raw, self._lock, self._state)
            except (serial.SerialException, OSError):
                print("[serial] Disconnected. Reconnecting...")
                with self._lock:
                    self._state["connected"] = False
                time.sleep(1)


# ── UDP reader thread ──────────────────────────────────────────────────────

class UdpReader:
    def __init__(self, port, state_lock, state):
        self.port = port
        self._lock = state_lock
        self._state = state
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()

    def _run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 256)
        except OSError:
            pass
        sock.bind(("0.0.0.0", self.port))
        sock.setblocking(False)

        with self._lock:
            self._state["connected"] = True
        print(f"[udp] Listening on 0.0.0.0:{self.port}")

        while not self._stop.is_set():
            ready, _, _ = select.select([sock], [], [], 0.05)
            if not ready:
                continue
            # Drain all queued datagrams, keep only the latest.
            latest = None
            try:
                while True:
                    data, _ = sock.recvfrom(256)
                    latest = data
            except BlockingIOError:
                pass
            if latest is not None:
                parse_q_packet(latest, self._lock, self._state)

        sock.close()


# ── OpenGL renderer ─────────────────────────────────────────────────────────

def init_gl(width, height):
    glEnable(GL_DEPTH_TEST)
    glEnable(GL_LIGHTING)
    glEnable(GL_LIGHT0)
    glEnable(GL_COLOR_MATERIAL)
    glEnable(GL_NORMALIZE)

    glLightfv(GL_LIGHT0, GL_POSITION, [2, 4, 3, 1])
    glLightfv(GL_LIGHT0, GL_DIFFUSE, [1.0, 1.0, 1.0, 1.0])
    glLightfv(GL_LIGHT0, GL_AMBIENT, [0.25, 0.25, 0.3, 1.0])
    glLightfv(GL_LIGHT0, GL_SPECULAR, [0.6, 0.6, 0.6, 1.0])

    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE)

    glClearColor(0.12, 0.12, 0.15, 1.0)
    glViewport(0, 0, width, height)
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluPerspective(45, width / height, 0.1, 50.0)
    glMatrixMode(GL_MODELVIEW)


def draw_axes(length=1.5):
    glDisable(GL_LIGHTING)
    glLineWidth(1.5)
    glBegin(GL_LINES)
    glColor3f(1, 0.2, 0.2); glVertex3f(0, 0, 0); glVertex3f(length, 0, 0)
    glColor3f(0.2, 1, 0.2); glVertex3f(0, 0, 0); glVertex3f(0, length, 0)
    glColor3f(0.3, 0.3, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, length)
    glEnd()
    glEnable(GL_LIGHTING)


def apply_ref_offset(quat: np.ndarray, ref_quat) -> None:
    if ref_quat is None:
        return
    rw, rx, ry, rz = ref_quat[0], ref_quat[1], ref_quat[2], ref_quat[3]
    qw, qx, qy, qz = quat[0], quat[1], quat[2], quat[3]
    cw, cx, cy, cz = rw, -rx, -ry, -rz
    w = cw * qw - cx * qx - cy * qy - cz * qz
    x = cw * qx + cx * qw + cy * qz - cz * qy
    y = cw * qy - cx * qz + cy * qw + cz * qx
    z = cw * qz + cx * qy - cy * qx + cz * qw
    quat[0], quat[1], quat[2], quat[3] = w, x, y, z


def main():
    parser = argparse.ArgumentParser(
        description="BNO055 IMU → STL Visualizer (serial or UDP)",
    )
    parser.add_argument(
        "port",
        nargs="?",
        default=None,
        help="Serial port (e.g. COM5). Omit when using --udp.",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--udp",
        type=int,
        default=None,
        metavar="PORT",
        help="Listen for UDP packets on this port instead of serial (e.g. --udp 4210)",
    )
    parser.add_argument("--stl", type=str, default=None)
    parser.add_argument("--vsync", action="store_true")
    parser.add_argument("--console-hz", type=float, default=0.0)
    parser.add_argument("--title-hz", type=float, default=10.0)
    args = parser.parse_args()

    if args.udp is None and args.port is None:
        parser.error("Provide a serial port or --udp PORT")

    if args.stl and Path(args.stl).exists():
        print(f"[mesh] Loading {args.stl}")
        vertices, normals = load_stl(args.stl)
        print(f"[mesh] {len(vertices) // 3} triangles → GPU VBO")
    else:
        if args.stl:
            print(f"[mesh] File not found: {args.stl} — using default box")
        else:
            print("[mesh] No --stl given, using default box")
        vertices, normals = generate_box()

    if not glfw.init():
        print("[glfw] init failed", file=sys.stderr)
        sys.exit(1)

    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 2)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 1)
    width, height = 1024, 768
    window = glfw.create_window(width, height, "IMU Visualizer", None, None)
    if not window:
        glfw.terminate()
        print("[glfw] create_window failed", file=sys.stderr)
        sys.exit(1)

    glfw.make_context_current(window)
    glfw.swap_interval(1 if args.vsync else 0)

    def on_resize(win, w, h):
        if h <= 0:
            return
        glViewport(0, 0, w, h)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45, w / h, 0.1, 50.0)
        glMatrixMode(GL_MODELVIEW)

    glfw.set_framebuffer_size_callback(window, on_resize)
    init_gl(width, height)

    mesh_obj = StaticInterleavedMesh(vertices, normals)

    state_lock = threading.Lock()
    state = {
        "quat": np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float64),
        "euler": np.array([0.0, 0.0, 0.0], dtype=np.float64),
        "cal": (0, 0, 0, 0),
        "connected": False,
        "pkt_count": 0,
    }

    if args.udp is not None:
        reader = UdpReader(args.udp, state_lock, state)
        source_label = f"UDP :{args.udp}"
    else:
        reader = SerialReader(args.port, args.baud, state_lock, state)
        source_label = f"Serial {args.port}"
    reader.start()

    ref_quat = None
    key_r_prev = False
    last_console = 0.0
    last_title = 0.0
    last_pkt_count = 0
    last_pps_time = time.perf_counter()
    pps_display = 0.0
    console_interval = 1.0 / args.console_hz if args.console_hz and args.console_hz > 0 else None
    title_interval = 1.0 / args.title_hz if args.title_hz and args.title_hz > 0 else None

    rot_mat = np.empty(16, dtype=np.float32)
    quat_work = np.empty(4, dtype=np.float64)

    print(
        f"[view] {source_label} | R = reset | ESC = quit | "
        + ("V-Sync ON" if args.vsync else "V-Sync OFF")
    )

    while not glfw.window_should_close(window):
        if glfw.get_key(window, glfw.KEY_ESCAPE) == glfw.PRESS:
            glfw.set_window_should_close(window, True)

        r_down = glfw.get_key(window, glfw.KEY_R) == glfw.PRESS
        if r_down and not key_r_prev:
            with state_lock:
                quat_work[:] = state["quat"]
            ref_quat = quat_work.copy()
            print("[view] Orientation reset")
        key_r_prev = r_down

        fbw, fbh = glfw.get_framebuffer_size(window)
        if fbw != width or fbh != height:
            width, height = fbw, fbh

        with state_lock:
            quat_work[:] = state["quat"]
            euler = state["euler"].copy()
            cal = state["cal"]
            connected = state["connected"]
            cur_pkt = state["pkt_count"]

        apply_ref_offset(quat_work, ref_quat)
        quat_to_gl_matrix(
            float(quat_work[0]),
            float(quat_work[1]),
            float(quat_work[2]),
            float(quat_work[3]),
            rot_mat,
        )

        now = time.perf_counter()
        dt_pps = now - last_pps_time
        if dt_pps >= 1.0:
            pps_display = (cur_pkt - last_pkt_count) / dt_pps
            last_pkt_count = cur_pkt
            last_pps_time = now

        if title_interval is None or (now - last_title) >= title_interval:
            last_title = now
            st = "OK" if connected else "WAITING"
            title = (
                f"IMU [{st}] {pps_display:.0f} pkt/s | "
                f"cal {cal[0]}/{cal[1]}/{cal[2]}/{cal[3]} | "
                f"h={euler[0]:.0f} r={euler[1]:.0f} p={euler[2]:.0f} | R reset ESC quit"
            )
            glfw.set_window_title(window, title[:250])

        if console_interval and (now - last_console) >= console_interval:
            last_console = now
            st = "OK" if connected else "WAITING"
            print(
                f"q=({quat_work[0]:+.3f},{quat_work[1]:+.3f},{quat_work[2]:+.3f},{quat_work[3]:+.3f}) "
                f"euler=({euler[0]:.1f},{euler[1]:.1f},{euler[2]:.1f}) cal={cal} {pps_display:.0f}pps {st}"
            )

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        glLoadIdentity()
        gluLookAt(0, 0, 5, 0, 0, 0, 0, 1, 0)

        draw_axes()

        glPushMatrix()
        glMultMatrixf(rot_mat)
        mesh_obj.draw()
        glPopMatrix()

        glfw.swap_buffers(window)
        glfw.poll_events()

    mesh_obj.delete()
    reader.stop()
    glfw.terminate()
    sys.exit(0)


if __name__ == "__main__":
    main()
