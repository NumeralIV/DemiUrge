"""Run a heavy command (render, Unreal launch, bake) only when one of N machine-wide slots is free.

    python tools/render_slot.py [--slots 5] [--cpu] [--timeout 3600] -- <command> [args...]

    --cpu  also wait while CPU load is over 70 % (use for CPU-heavy jobs such as Cycles CPU renders or bakes)

Slots are lock files in %TEMP%/demiurge-render-slots holding the owner's PID; slots of dead
processes are reclaimed. A job also waits while the machine is busier than the owner allows
(GPU utilisation over 80 % or CPU load over 70 %, sampled; owner's limits, 2026-11-19).
The command's exit code is returned.
"""
import os
import subprocess
import sys
import tempfile
import time

def pid_alive(pid):
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(h)
        return code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False

GPU_MAX, CPU_MAX = 60, 65  # owner: CPU 50-65 % by day (75-80 % was for the night of 2026-11-20 only); GPU 50-65 %

def machine_busy(check_cpu=False):
    """True while GPU utilisation (and, for CPU-heavy jobs, CPU load) is above the owner's limits."""
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=10).stdout
        if out.strip() and max(int(x) for x in out.split()) > GPU_MAX:
            return True
    except Exception:
        pass
    if not check_cpu:
        return False
    try:
        # % Processor Time, not Win32_Processor.LoadPercentage: that tracks % Processor Utility, which scales
        # with turbo frequency and reads 100 % while the real load is about 65 %.
        out = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "[int](Get-Counter '\\Processor(_Total)\\% Processor Time' -SampleInterval 2 -MaxSamples 1).CounterSamples[0].CookedValue"],
                             capture_output=True, text=True, timeout=20).stdout
        if out.strip() and float(out.strip()) > CPU_MAX:
            return True
    except Exception:
        pass
    return False

def main():
    args = sys.argv[1:]
    slots, timeout, cpu = 3, 3600.0, True  # 3 heavy jobs, wait for CPU < 65 %
    while args and args[0] != "--":
        if args[0] == "--slots":
            slots = int(args[1]); args = args[2:]
        elif args[0] == "--cpu":
            cpu = True; args = args[1:]
        elif args[0] == "--timeout":
            timeout = float(args[1]); args = args[2:]
        else:
            break
    if args and args[0] == "--":
        args = args[1:]
    if not args:
        print(__doc__)
        return 2
    d = os.path.join(tempfile.gettempdir(), "demiurge-render-slots")
    os.makedirs(d, exist_ok=True)
    me = os.getpid()
    t0 = time.time()
    held = None
    while held is None:
        if machine_busy(cpu):
            if time.time() - t0 > timeout:
                print("render_slot: timed out waiting for the machine", file=sys.stderr)
                return 3
            time.sleep(10)
            continue
        for i in range(slots):
            p = os.path.join(d, "slot%d.lock" % i)
            try:
                fd = os.open(p, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(fd, str(me).encode()); os.close(fd)
                held = p
                break
            except FileExistsError:
                try:
                    with open(p) as f:
                        owner = int(f.read().strip() or 0)
                except (OSError, ValueError):
                    owner = 0
                if not pid_alive(owner):
                    try:
                        os.remove(p)
                    except OSError:
                        pass
        if held is None:
            if time.time() - t0 > timeout:
                print("render_slot: timed out waiting for a slot", file=sys.stderr)
                return 3
            time.sleep(3)
    waited = time.time() - t0
    if waited > 5:
        print("render_slot: waited %.0f s for %s" % (waited, os.path.basename(held)), file=sys.stderr)
    try:
        return subprocess.call(args)
    finally:
        try:
            os.remove(held)
        except OSError:
            pass

if __name__ == "__main__":
    sys.exit(main())
