# Opens PATH as soon as it exists, holds the fd, and reports what it sees each second for SECS.
# Usage: python3 hold-open.py PATH SECONDS
import hashlib, os, sys, time
path, secs = sys.argv[1], float(sys.argv[2])
while not os.path.exists(path): time.sleep(0.1)
fd = os.open(path, os.O_RDONLY)
print(time.strftime('%H:%M:%S'), "opened", path, "pid", os.getpid(), flush=True)
t0 = time.time(); last = None
while time.time() - t0 < secs:
    st = os.fstat(fd)
    link = os.readlink(f"/proc/self/fd/{fd}")
    key = (st.st_nlink, st.st_size, st.st_blocks * 512, link)
    if key != last:
        print(time.strftime('%H:%M:%S'), "nlink", st.st_nlink, "size", st.st_size, "alloc", st.st_blocks * 512, "->", link, flush=True); last = key
    time.sleep(1)
os.lseek(fd, 0, 0); h = hashlib.sha1()
while True:
    b = os.read(fd, 1 << 20)
    if not b: break
    h.update(b)
print(time.strftime('%H:%M:%S'), "read through held fd: sha1", h.hexdigest(), flush=True)
os.close(fd); print(time.strftime('%H:%M:%S'), "closed", flush=True)
