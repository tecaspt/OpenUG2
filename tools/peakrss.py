# Reports exit status, wall time and peak RSS of a command.
# Replaces `/usr/bin/time -v`, which is not installed on this host and is not in
# the allowed package set. Adapted from openug2-dev/scripts/peakrss.py.
import resource, subprocess, sys, time
t = time.time()
r = subprocess.run(sys.argv[1:])
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
sys.stderr.write(f"[peakrss] exit={r.returncode} wall={time.time()-t:.1f}s maxrss={ru.ru_maxrss/1024:.0f}MB\n")
sys.exit(r.returncode)
