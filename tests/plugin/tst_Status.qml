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

    // omarchy-console's rail moves a widget to its running zone while consoleAwake. With the
    // defaults (and with no options at all), today's rule: downloads and an unreachable server.
    function test_console_reading_wakes_on_downloads_and_an_unreachable_server() {
        var defaults = Status.barOptions({});
        verify(defaults.wakeOnDownload);
        verify(!defaults.wakeOnUpload);
        verify(!defaults.wakeOnError);
        [undefined, defaults].forEach(function(o) {
            var s = sample();
            var r = Status.consoleReading(s, now, o);
            verify(r.awake);
            compare(r.state, "running");
            compare(r.value, "\u2193 1.2M \u2191 2.3M");
            // Seeding alone is the library's background: idle, still with its value.
            s.speed.dl = 0;
            r = Status.consoleReading(s, now, o);
            verify(!r.awake);
            compare(r.state, "");
            compare(r.value, "\u2193 0 \u2191 2.3M");
            // Torrents in error (missing files can last for months) do not wake it either.
            s.counts.errored = 5;
            verify(!Status.consoleReading(s, now, o).awake);
            // The server out of reach is a failure, with its reason.
            s.server.reachable = false;
            r = Status.consoleReading(s, now, o);
            verify(r.awake);
            compare(r.state, "failed");
            compare(r.reason, "qBittorrent unreachable");
            // No app: idle and quiet.
            r = Status.consoleReading(null, now, o);
            verify(!r.awake);
            compare(r.value, "");
        });
    }

    // docs/27 D14: each switch on its own.
    function test_console_reading_follows_each_wake_switch() {
        var none = Status.barOptions({ wakeOnDownload: false });
        var s = sample();                       // downloading and uploading
        verify(!Status.consoleReading(s, now, none).awake);

        var up = Status.barOptions({ wakeOnDownload: false, wakeOnUpload: true });
        var r = Status.consoleReading(s, now, up);
        verify(r.awake);
        compare(r.state, "running");
        s.speed.up = 0;                         // downloading only: uploads don't see it
        verify(!Status.consoleReading(s, now, up).awake);
        s.speed = { dl: 0, up: 2048 };          // seeding only
        verify(Status.consoleReading(s, now, up).awake);
        verify(!Status.consoleReading(s, now, Status.barOptions({})).awake);

        var err = Status.barOptions({ wakeOnDownload: false, wakeOnError: true });
        s = sample();
        verify(!Status.consoleReading(s, now, err).awake);
        s.counts.errored = 2;
        r = Status.consoleReading(s, now, err);
        verify(r.awake);
        compare(r.state, "failed");
        compare(r.reason, "2 errored");
        compare(r.value, "\u2193 1.2M \u2191 2.3M");
    }

    // D15 a: errors and a download at once read as the failure.
    function test_console_reading_errors_win_over_a_download() {
        var s = sample();
        s.counts.errored = 1;
        var r = Status.consoleReading(s, now, Status.barOptions({ wakeOnError: true }));
        compare(r.state, "failed");
        compare(r.reason, "1 errored");
        // Switched off (as `omarchy bar set` may store it): the download wakes it as before.
        r = Status.consoleReading(s, now, Status.barOptions({ wakeOnError: "false" }));
        compare(r.state, "running");
    }

    // An unreachable server wakes it whatever the switches say.
    function test_console_reading_unreachable_always_wakes() {
        var off = Status.barOptions({ wakeOnDownload: false, wakeOnUpload: false, wakeOnError: false });
        var s = sample();
        s.server.reachable = false;
        var r = Status.consoleReading(s, now, off);
        verify(r.awake);
        compare(r.state, "failed");
        compare(r.reason, "qBittorrent unreachable");
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

    // #80: the bar button's accessible description says the state in words that change only
    // when the state does, never the speeds the tooltip carries.
    function test_bar_description_is_coarse() {
        var s = sample();
        compare(Status.barDescription(s, now), "home: downloading, uploading");
        s.speed = { dl: 1, up: 0 };
        compare(Status.barDescription(s, now), "home: downloading");
        s.speed = { dl: 0, up: 0 };
        compare(Status.barDescription(s, now), "home: idle");
        s.alt = { on: true, kind: "burst" };
        compare(Status.barDescription(s, now), "home: idle, burst on");
        s.alt = { on: true };
        compare(Status.barDescription(s, now), "home: idle, alternative speed limits on");
        s.counts.errored = 1;
        compare(Status.barDescription(s, now), "home: torrents in error, alternative speed limits on");
        s.server.reachable = false;
        compare(Status.barDescription(s, now), "home: unreachable");
        compare(Status.barDescription(null, now), "not running");
    }

    function test_alt_button_name_has_no_key() {
        var s = sample();
        compare(Status.altButtonName(s), "Alternative speed limits");
        s.alt = { on: true, kind: "turtle" };
        compare(Status.altButtonName(s), "Turtle");
        compare(Status.altButtonTip(s), "Turtle on (A)");
    }

    function test_spoken_lines_have_words_for_arrows() {
        compare(Status.spoken("burst · ↓ ∞ · ↑ ∞ · until 07:00"), "burst, down unlimited, up unlimited, until 07:00");
        compare(Status.spoken("· until 07:00"), "until 07:00");
        compare(Status.spoken("→ /data · automatic management"), "to /data, automatic management");
        compare(Status.spoken(""), "");
    }

    function test_server_words_are_not_capitals() {
        var s = sample();
        compare(Status.serverWords(s), "home · qBittorrent 5.2.3");
        compare(Status.serverLine(s), "HOME · QBITTORRENT 5.2.3");
        compare(Status.serverWords(null), "omaseed");
    }

    // DECISIONS C20 (#127): the version gives way first, then "qBittorrent"; the name stays.
    function test_server_line_gives_way_version_first() {
        var s = sample();
        var forms = Status.serverLineForms(s);
        compare(forms, ["HOME · QBITTORRENT 5.2.3", "HOME · QBITTORRENT", "HOME"]);
        compare(forms[0], Status.serverLine(s));
        // One unit a character, for the sums.
        var widths = forms.map(function(f) { return f.length });
        compare(Status.fittingForm(forms, widths, 40), "HOME · QBITTORRENT 5.2.3");
        compare(Status.fittingForm(forms, widths, 24), "HOME · QBITTORRENT 5.2.3");
        compare(Status.fittingForm(forms, widths, 23), "HOME · QBITTORRENT");
        compare(Status.fittingForm(forms, widths, 18), "HOME · QBITTORRENT");
        compare(Status.fittingForm(forms, widths, 17), "HOME");
        // Not even the name: the name, which the popup elides.
        compare(Status.fittingForm(forms, widths, 2), "HOME");
        // No version known: the name is the only form.
        s.server.version = "";
        compare(Status.serverLineForms(s), ["HOME"]);
        compare(Status.serverLineForms(null), ["OMASEED"]);
        compare(Status.fittingForm(["NOT RUNNING"], [80, 0, 0], 40), "NOT RUNNING");
        compare(Status.fittingForm([], [], 40), "");
    }

    // docs/31 D12 (W1–W3): the widget follows the word the app writes. A file from before
    // alt.kind (or a mixed set, "alt") keeps today's yellow alternative limits.
    function test_alt_follows_the_word() {
        var s = sample();
        s.alt.on = true;
        compare(Status.altKind(s), "alt");
        compare(Status.altLine(s), "alternative limits · ↓ 1 MB/s · ↑ 200 KB/s");
        compare(Status.altButtonTip(s), "Alternative speed limits on (A)");
        compare(Status.markState(s, now).square, "alt");

        s.alt = { on: true, dlLimit: 0, upLimit: 0, kind: "burst", until: "07:00", byHand: false };
        compare(Status.altKind(s), "burst");
        compare(Status.altLine(s), "burst · ↓ ∞ · ↑ ∞ · until 07:00");
        compare(Status.altLine(s, false), "burst · ↓ ∞ · ↑ ∞");
        compare(Status.altUntilText(s), "· until 07:00");
        compare(Status.altButtonTip(s), "Burst on (A)");
        compare(Status.markState(s, now).square, "burst");
        var lines = Status.tooltip(s, now, Status.barOptions({})).split("\n");
        compare(lines[2], "burst · ↓ ∞ · ↑ ∞ · until 07:00");
        compare(lines[3], "L: panel  M: burst  R: open omaseed");
        // Off, the click is still named by what it does.
        s.alt.on = false;
        compare(Status.markState(s, now).square, "plain");
        compare(Status.altButtonTip(s), "Burst (A)");
        lines = Status.tooltip(s, now, Status.barOptions({ rightClick: "altSpeed", middleClick: "none" })).split("\n");
        compare(lines[2], "L: panel  R: burst");

        s.alt = { on: true, dlLimit: 10240, upLimit: 10240, kind: "turtle", until: "", byHand: false };
        compare(Status.altLine(s), "turtle · ↓ 10 KB/s · ↑ 10 KB/s");
        compare(Status.altUntilText(s), "");
        compare(Status.altButtonTip(s), "Turtle on (A)");
        compare(Status.markState(s, now).square, "alt");
        // Not running: today's words in the click map.
        lines = Status.tooltip(null, now, Status.barOptions({})).split("\n");
        compare(lines[1], "L: panel  M: alt-speed  R: open omaseed");
        // Settings keeps "alt-speed" as the option's name.
        var middle = Status.settingRows().find(function(row) { return row.key === "middleClick" });
        compare(middle.options[0][1], "alt-speed");
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

    function test_setting_rows_are_the_manifests_twelve() {
        var rows = Status.settingRows();
        var keys = rows.map(function(r) { return r.key });
        compare(keys, ["showMark", "showSpeed", "speedMetric", "markColor", "hideWhenIdle",
                       "middleClick", "rightClick", "rareSeeds", "recentCount",
                       "wakeOnDownload", "wakeOnUpload", "wakeOnError"]);
        // The rail's three are the last group, switches, with a line saying who reads them.
        compare(rows[9].group, "omarchy-console's rail");
        compare(rows[11].group, "omarchy-console's rail");
        compare(rows[10].type, "bool");
        verify(Status.settingGroupNote("omarchy-console's rail").indexOf("unreachable always wakes it") >= 0);
        compare(Status.settingGroupNote("In the bar"), "");
        // A switch flips, and a value stored as text by `omarchy bar set` reads the same.
        compare(Status.settingStep(rows[10], Status.barOptions({}), 1), true);
        compare(Status.settingStep(rows[9], Status.barOptions({}), 1), false);
        verify(Status.barOptions({ wakeOnUpload: "true" }).wakeOnUpload);
    }

    // Every key of the manifest's schema is a row and every row a key there, with the same defaults.
    function test_manifest_and_rows_agree() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", Qt.resolvedUrl("../../plugin/manifest.json"), false);
        xhr.send();
        var manifest = JSON.parse(xhr.responseText);
        var schemaKeys = manifest.barWidget.schema.map(function(e) { return e.key });
        var rowKeys = Status.settingRows().map(function(r) { return r.key });
        compare(rowKeys.slice().sort(), schemaKeys.slice().sort());
        var defaults = Status.barOptions({});
        for (var k in manifest.barWidget.defaults)
            compare(defaults[k], manifest.barWidget.defaults[k], k);
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

    // docs/33 §11 (D24, #73): the servers and the shared menu the app writes within v2.
    function menuSample() {
        var status = sample();
        status.servers = [
            { id: "a", name: "public", current: false, state: "ok", count: 0, seen: now - 60, reason: "" },
            { id: "b", name: "private", current: true, state: "ok", count: 4, seen: now, reason: "" },
            { id: "c", name: "seedbox", current: false, state: "refused", count: -1, seen: now - 600, reason: "sign-in refused" }
        ];
        status.menu = [
            { id: "servers", label: "servers", kind: "caption", checked: false, enabled: false, detail: "", urgent: false },
            { id: "server:a", label: "public", kind: "radio", checked: false, enabled: true, detail: "0 torrents", urgent: false },
            { id: "server:b", label: "private", kind: "radio", checked: true, enabled: true, detail: "4 torrents", urgent: false },
            { id: "server:c", label: "seedbox", kind: "radio", checked: false, enabled: true, detail: "sign-in refused", urgent: true },
            { id: "separator:servers", label: "", kind: "separator", checked: false, enabled: false, detail: "", urgent: false },
            { id: "alt-speed", label: "Alternative Speed Limits", kind: "check", checked: false, enabled: true, detail: "", urgent: false },
            { id: "stop-all", label: "Stop All", kind: "action", checked: false, enabled: false, detail: "", urgent: false },
            { id: "start-all", label: "Start All", kind: "action", checked: false, enabled: true, detail: "", urgent: false },
            { id: "separator:actions", label: "", kind: "separator", checked: false, enabled: false, detail: "", urgent: false },
            { id: "add", label: "Add Torrent…", kind: "action", checked: false, enabled: true, detail: "", urgent: false },
            { id: "future", label: "Something New", kind: "action", checked: false, enabled: true, detail: "", urgent: false },
            { id: "odd", label: "?", kind: "slider", checked: false, enabled: true, detail: "", urgent: false },
            { id: "separator:quit", label: "", kind: "separator", checked: false, enabled: false, detail: "", urgent: false },
            { id: "quit", label: "Quit omaseed", kind: "action", checked: false, enabled: true, detail: "", urgent: false }
        ];
        return status;
    }

    function test_an_app_without_the_menu_shows_the_popup_as_before() {
        var status = sample();
        compare(Status.servers(status).length, 0);
        verify(!Status.canSwitch(status));
        compare(Status.menuRows(status).length, 0);
        compare(Status.menuRows(null, "servers").length, 0);
    }

    function test_the_servers_and_the_switch() {
        var status = menuSample();
        compare(Status.servers(status).length, 3);
        verify(Status.canSwitch(status));
        status.servers = [status.servers[1]];
        verify(!Status.canSwitch(status)); // D19: one server never offers to switch
    }

    function test_the_menu_is_drawn_as_written() {
        var rows = Status.menuRows(menuSample());
        var ids = rows.map(function(r) { return r.id; });
        // A row the widget never heard of is drawn; a kind it cannot draw is left out.
        compare(ids, ["servers", "server:a", "server:b", "server:c", "separator:servers", "alt-speed", "stop-all",
                      "start-all", "separator:actions", "add", "future", "separator:quit", "quit"]);
        // The cursor skips captions, separators and disabled rows.
        compare(Status.menuTargets(rows), [1, 2, 3, 5, 7, 9, 10, 12]);
        var servers = Status.menuRows(menuSample(), "servers");
        compare(servers.map(function(r) { return r.id; }), ["server:a", "server:b", "server:c"]);
        verify(servers[1].checked);
        verify(servers[2].urgent);
    }

    function test_separators_never_lead_trail_or_double() {
        var status = menuSample();
        status.menu = [status.menu[4], status.menu[5], status.menu[8], status.menu[12], status.menu[13], status.menu[12]];
        compare(Status.menuRows(status).map(function(r) { return r.kind; }), ["check", "separator", "action"]);
    }

    function test_what_a_pick_does() {
        compare(Status.menuEffect("server:b"), "switch");
        compare(Status.menuServerId("server:b"), "b");
        compare(Status.menuServerId("add"), "");
        compare(Status.menuEffect("alt-speed"), "stay");
        compare(Status.menuEffect("stop-all"), "stay");
        compare(Status.menuEffect("start-all"), "stay");
        compare(Status.menuEffect("add"), "close");
        compare(Status.menuEffect("settings"), "close");
        compare(Status.menuEffect("quit"), "close");
        compare(Status.menuKey("alt-speed"), "A");
        compare(Status.menuKey("add"), "m");
        compare(Status.menuKey("quit"), "");
    }
}
