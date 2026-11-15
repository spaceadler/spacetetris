"""play the real game through a pseudo-terminal, the way a player would:
start a game, play to game over, 'y' for a fresh one, play to game over,
'n' to quit. passes when every step is seen, the exit code is 0, and the
terminal is left with line mode and echo back on.

keys go at ~16/s, below the frame rate, so no input queue builds up.
POSIX only (the pty module). usage: python3 -I tools/pty_game_test.py <binary>
exit status: 0 when every check passed, 1 otherwise."""
import os, pty, select, sys, termios, time

binary = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.execv(binary, [binary])

buf = b""
def pump(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.01)
        if r:
            try:
                buf += os.read(fd, 65536)
            except OSError:
                return

def wait_for(text, timeout, start):
    end = time.time() + timeout
    while time.time() < end:
        pump(0.05)
        i = buf.find(text, start)
        if i >= 0:
            return i
    return -1

FAILED = []


def check(label, ok):
    print(f"   {'PASS' if ok else 'FAIL'}  {label}")
    if not ok:
        FAILED.append(label)

def play_until_game_over(start, keys, timeout=120):
    end, i = time.time() + timeout, 0
    while time.time() < end:
        os.write(fd, keys[i % len(keys)].encode())
        i += 1
        pump(0.06)
        g = buf.find(b"GAME OVER", start)
        if g >= 0:
            return g
    return -1

def frame_after_without_game_over(start, timeout=3):
    # find a complete frame (between two cursor-home codes) after `start`
    # that has no GAME OVER overlay: proof the restart happened
    end = time.time() + timeout
    while time.time() < end:
        pump(0.05)
        a = buf.find(b"\x1b[H", start)
        while a >= 0:
            b = buf.find(b"\x1b[H", a + 3)
            if b < 0:
                break
            if b"GAME OVER" not in buf[a:b] and b"Score:" in buf[a:b]:
                return True
            a = b
    return False

p = wait_for(b"Selection: ", 3, 0)
os.write(fd, b"1\r")
p = wait_for(b"Score:", 3, p)
check("game starts", p > 0)
t0 = time.time()
g1 = play_until_game_over(p, "ssasdsswss")
check(f"game 1 reaches GAME OVER ({time.time() - t0:.0f}s)", g1 > 0)
pump(1.0)                      # let any queued keys drain
mark = len(buf)
os.write(fd, b"y")
check("'y' starts a fresh game (a frame with no GAME OVER)", frame_after_without_game_over(mark))
g2 = play_until_game_over(len(buf), "sdsswsas")
check("game 2 reaches GAME OVER", g2 > 0)
pump(1.0)
os.write(fd, b"n")
t_n, status = time.time(), None
while time.time() < t_n + 10:
    pump(0.05)
    done, st = os.waitpid(pid, os.WNOHANG)
    if done:
        status = st
        break
ok = status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0
check(f"'n' quits with exit code 0 ({time.time() - t_n:.1f}s)", ok)
if not ok and status is None:
    os.kill(pid, 9)
lflag = termios.tcgetattr(fd)[3]
check("terminal left with ICANON + ECHO on", bool(lflag & termios.ICANON) and bool(lflag & termios.ECHO))
sys.exit(1 if FAILED else 0)
