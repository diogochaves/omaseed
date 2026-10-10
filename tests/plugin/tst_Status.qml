import QtQuick
import QtTest
import "../../plugin/Status.js" as Status

// #15. What the widget reads off the status file (plugin/STATUS.md, schema v2): the
// not-running rule (Q12, 30 s), the states, the mark, the options' one cross rule, the
// keeping-alive sums for `rareSeeds`, and what Add accepts.
TestCase {
    name: "PluginStatus"

    readonly property int now: 1789346626

    function sample() {
        return {
            v: 2,
            t: now - 2,
            server: { name: "home", url: "127.0.0.1:8081", version: "5.2.3", reachable: true, error: "", retryIn: 0 },
            reachable: "connected",
            speed: { dl: 1237319, up: 2422210 },
            alt: { on: false, dlLimit: 1048576, upLimit: 204800 },
            counts: { all: 142, downloading: 6, seeding: 118, completed: 121, stopped: 12, stalled: 3, checking: 1, errored: 0 },
            disk: { free: 1978765344768 },
            share: { sessionUp: 15247133491, sessionDown: 3328599654, sessionRatio: 4.58, allTimeUp: 1, allTimeDown: 1, allTimeRatio: 2.31, onOffer: 2309236834304 },
            uploadHour: [0, 0, 1200],
            week: [{ day: "2026-09-26", up: 10 }, { day: "2026-10-02", up: 20 }],
            alive: {
                buckets: [{ seeds: 1, count: 2, waiting: 2, peers: 11, quiet: 1 }, { seeds: 2, count: 5, waiting: 3, peers: 30, quiet: 2 },
                          { seeds: 3, count: 9, waiting: 1, peers: 4, quiet: 0 }],
                rows: [{ hash: "a", name: "Prelinger", seeds: 1, waiting: 9 }, { hash: "b", name: "debian", seeds: 1, waiting: 2 },
                       { hash: "c", name: "Field", seeds: 2, waiting: 14 }, { hash: "d", name: "three", seeds: 3, waiting: 40 }]
            },
            topSeeders: [],
            active: [{ hash: "x", name: "archlinux-2026.10.01-x86_64.iso", progress: 0.489, dl: 1929379, eta: 160 }],
            recent: [{ kind: "completed", hash: "r", name: "ubuntu-26.04.1-desktop-amd64.iso", at: now - 720, size: 6560000000, category: "linux", path: "/data/u.iso" },
                     { kind: "milestone", hash: "m", name: "Tears of Steel", at: now - 3600, ratio: 1, path: "" }],
            categories: [{ name: "linux", savePath: "/data/torrents/linux" }, { name: "movies", savePath: "" }],
            add: { savePath: "/data/torrents", autoTmm: true, category: "linux" },
            downloads: []
        };
    }

    function test_parse_rejects_what_is_not_an_object() {
        compare(Status.parse("not json"), null);
        compare(Status.parse("[1,2]"), null);
        compare(Status.parse(""), null);
        compare(Status.parse('{"v":2}').v, 2);
    }

    function test_not_running_when_missing_stale_or_another_schema() {
        compare(Status.state(null, now), "notRunning");
        var s = sample();
        compare(Status.state(s, now), "moving");
        s.t = now - 31;
        compare(Status.state(s, now), "notRunning");
        s.t = now - 30;
        compare(Status.state(s, now), "moving");
        s.v = 1;
        compare(Status.state(s, now), "notRunning");
        s.v = 3;
        compare(Status.state(s, now), "notRunning");
    }

    function test_states() {
        var s = sample();
        s.speed = { dl: 0, up: 0 };
        compare(Status.state(s, now), "idle");
        s.counts.errored = 2;
        compare(Status.state(s, now), "error");
        s.server.reachable = false;
        compare(Status.state(s, now), "unreachable");
    }

    function test_mark_lights_each_direction() {
        var s = sample();
        var m = Status.markState(s, now);
        verify(m.down && m.up && !m.dim);
        compare(m.square, "plain");
        s.speed.dl = 0;
        m = Status.markState(s, now);
        verify(!m.down && m.up);
        s.alt.on = true;
        compare(Status.markState(s, now).square, "alt");
        s.counts.errored = 1;
        compare(Status.markState(s, now).square, "error");
        s.server.reachable = false;
        m = Status.markState(s, now);
        compare(m.square, "unreachable");
        verify(!m.down && !m.up);
        m = Status.markState(null, now);
        verify(m.dim && !m.down && !m.up);
    }

    // omarchy-console's rail moves a widget to its running zone while consoleAwake.
    function test_console_reading_wakes_on_downloads_and_an_unreachable_server() {
        var s = sample();
        var r = Status.consoleReading(s, now);
        verify(r.awake);
        compare(r.state, "running");
        compare(r.value, "\u2193 1.2M \u2191 2.3M");
        // Seeding alone is the library's background: idle, still with its value.
        s.speed.dl = 0;
        r = Status.consoleReading(s, now);
        verify(!r.awake);
        compare(r.state, "");
        compare(r.value, "\u2193 0 \u2191 2.3M");
        // Torrents in error (missing files can last for months) do not wake it either.
        s.counts.errored = 5;
        verify(!Status.consoleReading(s, now).awake);
        // The server out of reach is a failure, with its reason.
        s.server.reachable = false;
        r = Status.consoleReading(s, now);
        verify(r.awake);
        compare(r.state, "failed");
        compare(r.reason, "qBittorrent unreachable");
        // No app: idle and quiet.
        r = Status.consoleReading(null, now);
        verify(!r.awake);
        compare(r.value, "");
    }

    function test_hide_when_idle_keeps_news() {
        var s = sample();
        verify(!Status.hiddenWhenIdle(s, now));
        s.speed = { dl: 0, up: 0 };
        verify(Status.hiddenWhenIdle(s, now));
        s.server.reachable = false;
        verify(!Status.hiddenWhenIdle(s, now));
    }

    function test_mark_and_speed_never_both_off() {
        var o = Status.barOptions({});
        verify(o.showMark && !o.showSpeed);
        compare(o.speedMetric, "both");
        compare(o.markColor, "directions");
        compare(o.middleClick, "altSpeed");
        compare(o.rightClick, "openApp");
        compare(o.rareSeeds, 2);
        compare(o.recentCount, 3);
        o = Status.barOptions({ showMark: false, showSpeed: false });
        verify(o.showMark && !o.showSpeed);
        o = Status.barOptions({ showMark: false, showSpeed: true });
        verify(!o.showMark && o.showSpeed);
        o = Status.barOptions({ rareSeeds: 9, recentCount: -2, middleClick: "pauseAll" });
        compare(o.rareSeeds, 5);
        compare(o.recentCount, 0);
        compare(o.middleClick, "altSpeed");
    }

    function test_keeping_alive_sums_up_to_the_threshold() {
        var a = Status.alive(sample(), 2);
        compare(a.held, 7);
        compare(a.onlyYou, 2);
        compare(a.plusOne, 5);
        compare(a.waiting, 5);
        compare(a.peers, 41);
        compare(a.quiet, 3);
        compare(a.rows.length, 3);
        compare(a.rows[0].name, "Prelinger");
        compare(Status.aliveSummary(a), "7 with 2 or fewer seeds · 2 only you");
        compare(Status.aliveRowText(a.rows[2]), "you + 1 · 14 waiting");
        a = Status.alive(sample(), 1);
        compare(a.held, 2);
        compare(a.rows.length, 2);
        a = Status.alive(sample(), 3);
        compare(a.held, 16);
        compare(a.rows.length, 3);
    }

    function test_tooltip() {
        var s = sample();
        var lines = Status.tooltip(s, now, Status.barOptions({})).split("\n");
        compare(lines[0], "home · ↓ 1.18 MB/s ↑ 2.31 MB/s · 6 downloading · 118 seeding");
        compare(lines[1], "gave 14.2 GB this session · ratio 4.58");
        compare(lines[2], "L: panel  M: alt-speed  R: open omaseed");
        s.alt.on = true;
        lines = Status.tooltip(s, now, Status.barOptions({ middleClick: "none" })).split("\n");
        compare(lines[2], "alternative limits · ↓ 1 MB/s · ↑ 200 KB/s");
        compare(lines[3], "L: panel  R: open omaseed");
        s.server.reachable = false;
        s.server.error = "connection refused";
        s.server.retryIn = 7;
        lines = Status.tooltip(s, now, Status.barOptions({})).split("\n");
        compare(lines[2], "unreachable · connection refused · retrying in 7 s");
        lines = Status.tooltip(null, now, Status.barOptions({})).split("\n");
        compare(lines[0], "omaseed is not running");
    }

    function test_section_summaries() {
        var s = sample();
        compare(Status.serverLine(s), "HOME · QBITTORRENT 5.2.3");
        compare(Status.shareSummary(s), "14.2 GB given this session · ratio 4.58");
        compare(Status.activeSummary(s), "1 · archlinux 48.9 %");
        compare(Status.recentSummary(s, 3, now), "ubuntu finished 12 min ago");
        compare(Status.recentMeta(s.recent[0], now), "finished · 6.11 GB · linux · 12 min ago");
        compare(Status.recentMeta(s.recent[1], now), "reached ratio 1.0 · 1 h ago");
        compare(Status.recent(s, 1).length, 1);
        compare(Status.recent(s, 0).length, 0);
        var bars = Status.weekBars(s.week);
        compare(bars.length, 2);
        compare(bars[1].height, 1);
        compare(bars[0].height, 0.5);
        verify(bars[1].today);
        compare(bars[0].day, "Sat");
    }

    function test_sparkline_hides_after_an_hour_of_nothing() {
        verify(Status.sparkVisible([0, 0, 1]));
        verify(!Status.sparkVisible([0, 0, 0]));
        verify(!Status.sparkVisible(undefined));
        compare(Status.peak([3, 9, 2]), 9);
    }

    function test_addable_keeps_torrent_sources_only() {
        var got = Status.addable("hello\nmagnet:?xt=urn:btih:ABC&dn=Big+Buck\n https://x.org/a.torrent \n"
                                 + "file:///home/me/Downloads/My%20file.torrent\n/home/me/b.torrent\n/home/me/notes.txt");
        compare(got.length, 4);
        compare(got[0], "magnet:?xt=urn:btih:ABC&dn=Big+Buck");
        compare(got[1], "https://x.org/a.torrent");
        compare(got[2], "/home/me/Downloads/My file.torrent");
        compare(got[3], "/home/me/b.torrent");
        compare(Status.sourceName(got[0]), "Big Buck");
        compare(Status.sourceName(got[2]), "My file.torrent");
        compare(Status.sourceKind(got[1]), "link");
        compare(Status.sourceName("magnet:?xt=urn:btih:0123456789ABCDEF"), "magnet 0123456789ab");
    }

    function test_target_folder_follows_automatic_management() {
        var s = sample();
        compare(Status.targetFolder(s, "linux"), "/data/torrents/linux");
        compare(Status.targetFolder(s, "movies"), "/data/torrents/movies");
        compare(Status.targetFolder(s, ""), "/data/torrents");
        // The server's own default does not matter: the widget's add always uses automatic
        // management, so the category's folder still applies.
        s.add.autoTmm = false;
        compare(Status.targetFolder(s, "linux"), "/data/torrents/linux");
    }

    function test_handed_over_rows_read_as_duplicates_at_once() {
        var result = { added: [{ source: "/d/a.torrent", name: "a", hash: "1" }],
                       duplicates: [{ source: "/d/b.torrent", name: "b", hash: "2" }],
                       failed: [{ source: "/d/c.torrent", error: "not a torrent" }] };
        var handed = Status.handedOver({}, result, "home");
        compare(Status.downloadDuplicateOn({ path: "/d/a.torrent", inLibrary: false }, "home", handed), "home");
        compare(Status.downloadDuplicateOn({ path: "/d/b.torrent", inLibrary: false }, "home", handed), "home");
        // A failed add stays offered.
        compare(Status.downloadDuplicateOn({ path: "/d/c.torrent", inLibrary: false }, "home", handed), "");
        // The status file has the last word once it says so.
        compare(Status.downloadDuplicateOn({ path: "/d/z.torrent", inLibrary: true }, "home", handed), "home");
        // Handed to another server: not a duplicate on this one.
        compare(Status.downloadDuplicateOn({ path: "/d/a.torrent", inLibrary: false }, "private", handed), "");
        // Earlier hand-overs are kept.
        var more = Status.handedOver(handed, { added: [{ source: "/d/d.torrent" }] }, "private");
        compare(more["/d/a.torrent"], "home");
        compare(more["/d/d.torrent"], "private");
        compare(Status.handedOver(null, null, "home"), {});
    }

    // `omarchy bar set` without --json stores text: the widget reads it as meant (docs/21).
    function test_options_read_text_values() {
        var o = Status.barOptions({ showSpeed: "true", showMark: "false", hideWhenIdle: "true",
                                    rareSeeds: "4", recentCount: "0" });
        compare(o.showSpeed, true);
        compare(o.showMark, false);
        compare(o.hideWhenIdle, true);
        compare(o.rareSeeds, 4);
        compare(o.recentCount, 0);
        compare(Status.barOptions({ showMark: "nonsense" }).showMark, true);
    }

    function test_setting_rows_are_the_manifests_nine() {
        var keys = Status.settingRows().map(function(r) { return r.key });
        compare(keys, ["showMark", "showSpeed", "speedMetric", "markColor", "hideWhenIdle",
                       "middleClick", "rightClick", "rareSeeds", "recentCount"]);
    }

    function test_the_last_of_mark_and_speed_cannot_be_switched_off() {
        var rows = Status.settingRows();
        var mark = rows[0], speed = rows[1], metric = rows[2];
        var cfg = Status.barOptions({});
        verify(Status.settingLocked(mark, cfg));
        compare(Status.settingStep(mark, cfg, 1), null);
        verify(Status.settingDetail(mark, cfg).indexOf("switch the speed on first") >= 0);
        // The direction means nothing with the speed off.
        verify(Status.settingInactive(metric, cfg));
        compare(Status.settingStep(metric, cfg, 1), null);
        compare(Status.settingDetail(metric, cfg), "with the speed on");

        compare(Status.settingStep(speed, cfg, 1), true);
        cfg = Status.barOptions({ showSpeed: true });
        verify(!Status.settingLocked(mark, cfg));
        compare(Status.settingStep(mark, cfg, 1), false);
        cfg = Status.barOptions({ showSpeed: true, showMark: false });
        verify(Status.settingLocked(speed, cfg));
        compare(Status.settingStep(speed, cfg, 1), null);
    }

    function test_steps_wrap_picks_and_stop_numbers() {
        var rows = Status.settingRows();
        var cfg = Status.barOptions({ showSpeed: true });
        compare(Status.settingStep(rows[2], cfg, 1), "down");   // both → down (wraps)
        compare(Status.settingStep(rows[2], cfg, -1), "up");
        compare(Status.settingStep(rows[5], cfg, 1), "openApp");
        compare(Status.settingStep(rows[7], cfg, 1), 3);
        compare(Status.settingStep(rows[7], cfg, -1), 1);
        compare(Status.settingStep(rows[7], Status.barOptions({ rareSeeds: 5 }), 1), null);
        compare(Status.settingStep(rows[8], Status.barOptions({ recentCount: 0 }), -1), null);
    }

    function test_settings_with_keeps_the_rest_of_the_entry() {
        var next = Status.settingsWith({ id: "chaves.omaseed", showSpeed: true, other: 1 }, "markColor", "single");
        compare(next, { showSpeed: true, other: 1, markColor: "single" });
        compare(Status.settingsWith(null, "showSpeed", false), { showSpeed: false });
    }

    function test_bus_string_reply() {
        var reply = JSON.stringify({ type: "s", data: [JSON.stringify([{ name: "x", duplicate: true }])] });
        compare(Status.busString(reply)[0].name, "x");
        compare(Status.busString("garbage"), null);
    }
}
