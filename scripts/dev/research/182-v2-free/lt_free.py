# omaseed #182: free a skipped file that holds downloaded pieces, then recheck or add again.
# Does the torrent read 100 % of what it wants afterwards?
#
# Usage: python3 -I lt_free.py v1|v2|hybrid WORKDIR MODE SKIP
#   MODE  keep-recheck    the skipped file stays; recheck
#         delete-recheck  stop, delete it, start, recheck (FreeSkippedJob on v1)
#         readd-after     remove the torrent, delete it, add again stopped with every file
#                         wanted, then skip it and start (AddAgainJob: priorities after the add)
#         readd-at        the same with the priorities in the add itself
#   SKIP  which real file is skipped: 0 a.mkv (3 MB), 1 b.nfo (70 kB), 2 c.clip (500 kB, last)
#
# Two libtorrent sessions on loopback, offline: a seeder of three files of random bytes whose
# sizes are not piece multiples, and a downloader that gets them all, as a download from a
# swarm does. WORKDIR is wiped first. qBittorrent's progress is total_wanted_done/total_wanted
# (1 when they are equal); printed as `progress`.
import os
import shutil
import sys
import time

import libtorrent as lt

kind, base, mode, skip = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
src = os.path.join(base, 'src')
dst = os.path.join(base, 'dst')
shutil.rmtree(base, ignore_errors=True)
os.makedirs(src)
os.makedirs(dst)
name = 'Fixture'
layout = [('a.mkv', 3_000_000), ('b.nfo', 70_001), ('c.clip', 500_003)]
for rel, size in layout:
    p = os.path.join(src, name, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(os.urandom(size))
fs = lt.file_storage()
lt.add_files(fs, os.path.join(src, name))
flags = {'hybrid': 0, 'v1': lt.create_torrent.v1_only, 'v2': lt.create_torrent.v2_only}[kind]
ct = lt.create_torrent(fs, 0, flags=flags)
lt.set_piece_hashes(ct, src)
tbytes = lt.bencode(ct.generate())
ti = lt.torrent_info(tbytes)


def sess():
    return lt.session({'listen_interfaces': '127.0.0.1:0', 'enable_dht': False,
                       'enable_lsd': False, 'enable_upnp': False, 'enable_natpmp': False,
                       'allow_multiple_connections_per_ip': True})


A, B = sess(), sess()


def add(s, path, prios=None, paused=False):
    p = lt.add_torrent_params()
    p.ti = lt.torrent_info(tbytes)
    p.save_path = path
    p.flags &= ~lt.torrent_flags.auto_managed
    if paused:
        p.flags |= lt.torrent_flags.paused
    else:
        p.flags &= ~lt.torrent_flags.paused
    if prios is not None:
        p.file_priorities = prios
    return s.add_torrent(p)


ha = add(A, src)
for _ in range(40):
    time.sleep(0.25)
    if ha.status().is_seeding:
        break
hb = add(B, dst)
time.sleep(0.3)
hb.connect_peer(('127.0.0.1', A.listen_port()))
for _ in range(120):
    time.sleep(0.25)
    if hb.status().is_seeding:
        break
f = hb.torrent_file().files()
real = [i for i in range(f.num_files()) if not (f.file_flags(i) & lt.file_storage.flag_pad_file)]
target = real[skip]
prios = [0 if i == target else 1 for i in range(f.num_files())]


def st(tag, h):
    t = h.status()
    done, wanted = t.total_wanted_done, t.total_wanted
    prog = 1.0 if wanted == done else done / max(wanted, 1)
    print(f'{kind:6} {mode:14} skip={f.file_path(target):15} {tag:12} '
          f'state={str(t.state):16} errc={t.errc.value()} pieces={t.num_pieces}/{ti.num_pieces()} '
          f'wanted={wanted} done={done} progress={prog:.4f}', flush=True)


def wait(h, n=40):
    for _ in range(n):
        time.sleep(0.25)
        if str(h.status().state) not in ('checking_files', 'checking_resume_data'):
            break
    time.sleep(0.5)


st('downloaded', hb)
hb.prioritize_files(prios)
time.sleep(0.5)
st('skipped', hb)
if mode == 'keep-recheck':
    hb.force_recheck()
    wait(hb)
    st('recheck', hb)
    sys.exit()
hb.pause()
time.sleep(0.5)
if mode == 'delete-recheck':
    os.remove(os.path.join(dst, f.file_path(target)))
    hb.resume()
    hb.force_recheck()
    wait(hb)
    st('recheck', hb)
elif mode in ('readd-after', 'readd-at'):
    B.remove_torrent(hb)
    time.sleep(1)
    os.remove(os.path.join(dst, f.file_path(target)))
    if mode == 'readd-at':
        hb = add(B, dst, prios, paused=True)
    else:
        hb = add(B, dst, None, paused=True)
        time.sleep(0.3)
        hb.prioritize_files(prios)
    time.sleep(0.5)
    hb.resume()
    wait(hb)
    st('added again', hb)
    hb.force_recheck()
    wait(hb)
    st('then recheck', hb)
