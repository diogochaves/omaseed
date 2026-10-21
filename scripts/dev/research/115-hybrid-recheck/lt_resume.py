# Load a qBittorrent fastresume (copied) into a standalone libtorrent session, recheck, report errc.
# Usage: python3 lt_resume.py COPY.fastresume SAVE_PATH [recheck|load]  (the .torrent beside it).
# Only on copies: never on a library's own BT_backup or its files.
import sys, time, libtorrent as lt
fr, savepath = sys.argv[1], sys.argv[2]
mode = sys.argv[3] if len(sys.argv) > 3 else 'recheck'
s = lt.session({'listen_interfaces': '127.0.0.1:0', 'enable_dht': False, 'enable_lsd': False,
                'enable_upnp': False, 'enable_natpmp': False,
                'alert_mask': lt.alert_category.error | lt.alert_category.status | lt.alert_category.storage | lt.alert_category.file_progress})
atp = lt.read_resume_data(open(fr, 'rb').read())
atp.save_path = savepath
atp.ti = lt.torrent_info(fr.replace('.fastresume', '.torrent'))
atp.trackers = []
atp.flags &= ~lt.torrent_flags.auto_managed & ~lt.torrent_flags.paused
h = s.add_torrent(atp)
def show(tag):
    for a in s.pop_alerts():
        if a.what() in ('file_completed',): continue
        print(f'  [{tag}] {a.what()}: {a.message()}')
    t = h.status()
    print(f'{tag}: state={t.state} errc={t.errc.category().name()}:{t.errc.value()}:{t.errc.message()!r} error_file={t.error_file} progress={t.progress:.3f} have={t.num_pieces} wanted={t.total_wanted} upload_mode={bool(t.flags & lt.torrent_flags.upload_mode)}', flush=True)
for i in range(6): time.sleep(0.5)
show('loaded')
if mode == 'recheck':
    h.force_recheck()
    for i in range(20): time.sleep(0.5)
    show('recheck')
    if h.status().errc.value():
        h.clear_error(); h.resume()
        for i in range(10): time.sleep(0.5)
        show('clear_error+resume')
