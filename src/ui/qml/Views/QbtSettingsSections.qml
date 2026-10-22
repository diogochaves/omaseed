import QtQuick

// qBittorrent's settings as omaseed draws them (docs/16 §4, §6): nine sections, each a list
// of rows, each row naming the `app/preferences` keys it reads and writes. The Settings
// modal's qBittorrent group renders these through `QbtSettingsPane`; nothing here draws.
//
// A row is an object with some of:
//   glyph, title, desc     what the row says
//   sub                    indented under the row above
//   dep                    "key" (on while that key is true), or "key!=value", or a list of
//                          those that must all hold: dims the row while one does not
//   toggle                 a switch for this boolean key
//   enable, disable        {key: value}: what turning the switch on (off) also writes.
//                          qBittorrent 5 ignores some switches sent alone: a share limit's
//                          `*_enabled` is on only while its value is 0 or more, and
//                          `random_port` goes off only with a `listen_port` to go back to.
//                          `enable` writes a key only while it holds its field's `none`, so
//                          a value already set is kept; "randomPort" is a random port
//   needs                  {keys: [...], note}: the switch turns on only once these keys
//                          hold something; until then it stays off and `note` says why
//   fields                 [{key, unit, scale, none, noneText, min, max, decimals, width,
//                          kind: "number" | "text" | "password" | "time", minKey, sep}]:
//                          `scale` divides the raw value for display (1024: bytes/s shown in
//                          KB/s); `none` is the raw value that means "no limit" and shows
//                          as an empty field with `noneText` (∞) as its placeholder; a
//                          "time" field writes `key` (hours) and `minKey` (minutes)
//   choice                 {key, options: [[value, text], …]}: chips, one of them lit
//   pick                   {key, options} or {key, source: "interfaces" | "addresses"}: a
//                          menu, for lists too long for chips
//   list                   {key, sep: "\n" | ";", dirs: bool}: a count and "edit…", which
//                          opens the list sheet; `dirs` is scan_dirs' {path: 0} object
//   button                 {text, action: "rotateKey" | "randomPort"}
//   reach                  shows qBittorrent's own reachability beside the port
//   confirm                "port" | "anon" | "iface" | "ipfilter" | "webui": asks first
//   defaults               {key: qBittorrent's default}: a "default" button while it differs
//   failure                "export": a yellow line under the row when qBittorrent's log says
//                          the last .torrent copy into one of its folders failed (docs/32 E1)
//   titleFor, descFor      {burst, turtle, alt}: the title or description for the way the
//                          alternative set goes (docs/31 §5, App.altSpeed.kind); a kind left
//                          out takes `title` / `desc`
//   altGlyph               the alt-speed gauge, its needle up for a burst set (docs/31 D4)
//   preset                 {key, text, about, changes}: a chip before the fields that writes
//                          `changes` at once; `key` presses it from anywhere in the section
// and three kinds that are not rows: {group: "…"} (a small heading), {note: "…", warn}, and
// {line: "set" | "schedule"}, docs/31's lines from App.altSpeed: which way the set goes, and
// what the schedule really does.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
QtObject {
    id: root

    // qBittorrent 5.2.3's defaults for the Advanced and log file keys, read from a fresh
    // profile (docs/16: "every key, with default buttons").
    readonly property var advancedDefaults: ({
            "announce_ip": "",
            "announce_port": 0,
            "async_io_threads": 10,
            "bdecode_depth_limit": 100,
            "bdecode_token_limit": 10000000,
            "block_peers_on_privileged_ports": false,
            "checking_memory_use": 32,
            "connection_speed": 30,
            "disk_cache": -1,
            "disk_cache_ttl": 60,
            "disk_io_read_mode": 1,
            "disk_io_type": 0,
            "disk_io_write_mode": 1,
            "disk_queue_size": 1048576,
            "embedded_tracker_port": 9000,
            "embedded_tracker_port_forwarding": false,
            "enable_coalesce_read_write": false,
            "enable_embedded_tracker": false,
            "enable_piece_extent_affinity": false,
            "enable_upload_suggestions": false,
            "file_log_age": 1,
            "file_log_age_type": 1,
            "file_log_backup_enabled": true,
            "file_log_delete_old": true,
            "file_log_enabled": true,
            "file_log_max_size": 65,
            "file_pool_size": 100,
            "hashing_threads": 1,
            "hostname_cache_ttl": 1200,
            "i2p_address": "127.0.0.1",
            "i2p_enabled": false,
            "i2p_inbound_length": 3,
            "i2p_inbound_quantity": 3,
            "i2p_mixed_mode": false,
            "i2p_outbound_length": 3,
            "i2p_outbound_quantity": 3,
            "i2p_port": 7656,
            "idn_support_enabled": false,
            "ignore_ssl_errors": false,
            "mark_of_the_web": true,
            "max_concurrent_http_announces": 50,
            "memory_working_set_limit": 512,
            "outgoing_ports_max": 0,
            "outgoing_ports_min": 0,
            "peer_tos": 1,
            "peer_turnover": 4,
            "peer_turnover_cutoff": 90,
            "peer_turnover_interval": 300,
            "performance_warning": false,
            "refresh_interval": 1500,
            "request_queue_size": 500,
            "resolve_peer_countries": true,
            "resolve_peer_host_names": false,
            "resume_data_storage_type": "Legacy",
            "save_resume_data_interval": 60,
            "save_statistics_interval": 15,
            "send_buffer_low_watermark": 10,
            "send_buffer_watermark": 500,
            "send_buffer_watermark_factor": 50,
            "socket_backlog_size": 30,
            "socket_receive_buffer_size": 0,
            "socket_send_buffer_size": 0,
            "ssl_enabled": false,
            "ssrf_mitigation": true,
            "stop_tracker_timeout": 2,
            "torrent_file_size_limit": 104857600,
            "upload_choking_algorithm": 1,
            "upload_slots_behavior": 0,
            "upnp_lease_duration": 0,
            "utp_tcp_mixed_mode": 0,
            "validate_https_tracker_certificate": true
        })

    readonly property string inf: "∞"

    /// A plain number field.
    function num(key: string, extra: var): var {
        const field = {
            "key": key,
            "kind": "number",
            "width": 70
        };
        for (const name in extra)
            field[name] = extra[name];
        return field;
    }

    /// A speed in KB/s over bytes/s, empty for no limit.
    function speed(key: string): var {
        return root.num(key, {
                            "unit": qsTr("KB/s"),
                            "scale": 1024,
                            "none": 0,
                            "noneText": root.inf,
                            "min": 0,
                            "width": 110
                        });
    }

    function text(key: string, width: int, placeholder: string): var {
        return {
            "key": key,
            "kind": "text",
            "width": width,
            "noneText": placeholder
        };
    }

    function secret(key: string): var {
        return {
            "key": key,
            "kind": "password",
            "width": 150,
            "noneText": qsTr("unchanged")
        };
    }

    /// An Advanced row: `key`'s own control, with qBittorrent's default to go back to.
    function adv(glyph: string, title: string, desc: string, row: var): var {
        const keys = [];
        if (row.toggle)
            keys.push(row.toggle);
        if (row.choice)
            keys.push(row.choice.key);
        for (const field of (row.fields || []))
            keys.push(field.key);
        const defaults = {};
        for (const key of keys) {
            if (root.advancedDefaults[key] !== undefined)
                defaults[key] = root.advancedDefaults[key];
        }
        row.glyph = glyph;
        row.title = title;
        row.desc = desc;
        row.defaults = defaults;
        return row;
    }

    readonly property var sections: [
        {
            "id": "speed",
            "glyph": "⇅",
            "name": qsTr("Speed"),
            "shortName": qsTr("Speed"),
            "sub": qsTr("global limits and the alt-speed schedule"),
            "rows": [
                {
                    "note": qsTr("The same four limits as ⋯ › speed limits. That sheet stays, for a quick change without opening Settings.")
                },
                {
                    "glyph": "↓",
                    "title": qsTr("Download limit"),
                    "desc": qsTr("For every torrent together. Empty means no limit."),
                    "fields": [root.speed("dl_limit")]
                },
                {
                    "glyph": "↑",
                    "title": qsTr("Upload limit"),
                    "fields": [root.speed("up_limit")]
                },
                {
                    "altGlyph": true,
                    "title": qsTr("Alt-speed download"),
                    "desc": qsTr("In force while alt-speed is on: A, the hero's chip, or the schedule below."),
                    "descFor": {
                        "burst": qsTr("In force while burst is on: A, the hero's chip, or the schedule below."),
                        "turtle": qsTr("In force while turtle is on: A, the hero's chip, or the schedule below.")
                    },
                    "fields": [root.speed("alt_dl_limit")]
                },
                {
                    "altGlyph": true,
                    "title": qsTr("Alt-speed upload"),
                    "fields": [root.speed("alt_up_limit")]
                },
                {
                    "line": "set"
                },
                {
                    "glyph": "◷",
                    "title": qsTr("Schedule alt-speed"),
                    "titleFor": {
                        "burst": qsTr("Schedule burst"),
                        "turtle": qsTr("Schedule turtle")
                    },
                    "desc": qsTr("Turns alt-speed on by itself between two times, and off again after."),
                    "descFor": {
                        "burst": qsTr("qBittorrent switches to burst by itself between two times, and back after."),
                        "turtle": qsTr("qBittorrent switches to turtle by itself between two times, and back after.")
                    },
                    "toggle": "scheduler_enabled"
                },
                {
                    "title": qsTr("From, to"),
                    "sub": true,
                    "dep": "scheduler_enabled",
                    // docs/31 D8: nights, every day, in one press. All four time keys go:
                    // qBittorrent ignores an hour sent without its minutes.
                    "preset": {
                        "key": "n",
                        "text": qsTr("nights"),
                        "about": qsTr("23:00 to 07:00, every day"),
                        "changes": {
                            "schedule_from_hour": 23,
                            "schedule_from_min": 0,
                            "schedule_to_hour": 7,
                            "schedule_to_min": 0,
                            "scheduler_days": 0
                        }
                    },
                    "fields": [{
                            "key": "schedule_from_hour",
                            "minKey": "schedule_from_min",
                            "kind": "time",
                            "width": 70
                        }, {
                            "key": "schedule_to_hour",
                            "minKey": "schedule_to_min",
                            "kind": "time",
                            "width": 70,
                            "sep": qsTr("to")
                        }]
                },
                {
                    "title": qsTr("Days"),
                    "sub": true,
                    "dep": "scheduler_enabled",
                    "pick": {
                        "key": "scheduler_days",
                        "options": [[0, qsTr("every day")], [1, qsTr("weekdays")], [2, qsTr("weekends")], [3, qsTr("Monday")], [4, qsTr("Tuesday")], [5, qsTr("Wednesday")], [6, qsTr("Thursday")], [7, qsTr("Friday")], [8, qsTr("Saturday")], [9, qsTr("Sunday")]]
                    }
                },
                {
                    "line": "schedule"
                },
                {
                    "glyph": "⇄",
                    "title": qsTr("Limit µTP too"),
                    "desc": qsTr("Apply the limits to µTP connections as well as TCP."),
                    "toggle": "limit_utp_rate"
                },
                {
                    "glyph": "⇄",
                    "title": qsTr("Count protocol overhead"),
                    "desc": qsTr("Count the traffic peers spend talking, not only the data."),
                    "toggle": "limit_tcp_overhead"
                },
                {
                    "glyph": "⌂",
                    "title": qsTr("Limit peers on your network"),
                    "desc": qsTr("Off lets computers on your own network go at full speed."),
                    "toggle": "limit_lan_peers"
                }
            ]
        },
        {
            "id": "discovery",
            "glyph": "◎",
            "name": qsTr("Discovery and privacy"),
            "shortName": qsTr("Discovery"),
            "sub": qsTr("how qBittorrent finds peers"),
            "rows": [
                {
                    "note": qsTr("Private torrents never use DHT, PeX or LSD, whatever these switches say. The torrent carries a private flag and qBittorrent obeys it. These three are for public torrents.")
                },
                {
                    "glyph": "◎",
                    "title": qsTr("DHT"),
                    "desc": qsTr("Find peers without a tracker, through a network of other clients."),
                    "toggle": "dht"
                },
                {
                    "title": qsTr("Bootstrap nodes"),
                    "desc": qsTr("Where DHT starts. Empty uses qBittorrent's own list."),
                    "sub": true,
                    "dep": "dht",
                    "fields": [root.text("dht_bootstrap_nodes", 220, qsTr("qBittorrent's list"))]
                },
                {
                    "glyph": "⇆",
                    "title": qsTr("Peer exchange (PeX)"),
                    "desc": qsTr("Swap peer lists with the peers you're connected to."),
                    "toggle": "pex"
                },
                {
                    "glyph": "⌂",
                    "title": qsTr("Local peer discovery (LSD)"),
                    "desc": qsTr("Find peers on your own network."),
                    "toggle": "lsd"
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("Encryption"),
                    "desc": qsTr("Prefer lets each peer choose. Require refuses peers that don't encrypt, which a few trackers ask for. Off refuses peers that do."),
                    "choice": {
                        "key": "encryption",
                        "options": [[0, qsTr("prefer")], [1, qsTr("require")], [2, qsTr("off")]]
                    }
                },
                {
                    "glyph": "!",
                    "title": qsTr("Anonymous mode"),
                    "desc": qsTr("Hides which client you use. Most private trackers allow only known clients, so this can get you refused. Keep it off unless a tracker says otherwise."),
                    "toggle": "anonymous_mode",
                    "confirm": "anon",
                    "warn": true
                },
                {
                    "glyph": "↗",
                    "title": qsTr("Announce to every tracker in a tier"),
                    "toggle": "announce_to_all_trackers"
                },
                {
                    "glyph": "↗",
                    "title": qsTr("Announce to every tier"),
                    "toggle": "announce_to_all_tiers"
                },
                {
                    "glyph": "+",
                    "title": qsTr("Add these trackers to new torrents"),
                    "desc": qsTr("Added to every new torrent. Private torrents usually forbid extra trackers, so leave this off if you use them."),
                    "toggle": "add_trackers_enabled"
                },
                {
                    "title": qsTr("Trackers"),
                    "sub": true,
                    "dep": "add_trackers_enabled",
                    "list": {
                        "key": "add_trackers",
                        "sep": "\n",
                        "what": qsTr("tracker URLs")
                    }
                },
                {
                    "glyph": "+",
                    "title": qsTr("Add trackers from a list"),
                    "desc": qsTr("Fetch a tracker list from a URL and add it to new torrents."),
                    "toggle": "add_trackers_from_url_enabled"
                },
                {
                    "title": qsTr("List URL"),
                    "sub": true,
                    "dep": "add_trackers_from_url_enabled",
                    "fields": [root.text("add_trackers_url", 220, "https://…")]
                }
            ]
        },
        {
            "id": "connection",
            "glyph": "⇌",
            "name": qsTr("Connection"),
            "shortName": qsTr("Connection"),
            "sub": qsTr("port, limits and blocking"),
            "rows": [
                {
                    "glyph": "⊙",
                    "title": qsTr("Listening port"),
                    "desc": qsTr("Peers connect to you here. Your router has to forward it, or UPnP has to."),
                    "fields": [root.num("listen_port", {
                                            "min": 0,
                                            "max": 65535
                                        })],
                    "reach": true,
                    "button": {
                        "text": qsTr("random"),
                        "action": "randomPort"
                    },
                    "confirm": "port"
                },
                {
                    "glyph": "⟳",
                    "title": qsTr("New random port on every start"),
                    "toggle": "random_port",
                    "disable": {
                        "listen_port": "randomPort"
                    }
                },
                {
                    "glyph": "⌬",
                    "title": qsTr("UPnP / NAT-PMP"),
                    "desc": qsTr("Ask the router to forward the port by itself."),
                    "toggle": "upnp"
                },
                {
                    "glyph": "⇄",
                    "title": qsTr("Protocol"),
                    "choice": {
                        "key": "bittorrent_protocol",
                        "options": [[0, qsTr("TCP and µTP")], [1, qsTr("TCP")], [2, qsTr("µTP")]]
                    }
                },
                {
                    "glyph": "⋈",
                    "title": qsTr("Connections"),
                    "desc": qsTr("At most, in total and per torrent. Empty means no limit."),
                    "fields": [root.num("max_connec", {
                                            "none": -1,
                                            "noneText": root.inf,
                                            "min": 1
                                        }), root.num("max_connec_per_torrent", {
                                                         "none": -1,
                                                         "noneText": root.inf,
                                                         "min": 1,
                                                         "sep": "·"
                                                     })]
                },
                {
                    "glyph": "↑",
                    "title": qsTr("Upload slots"),
                    "desc": qsTr("Peers you upload to at once, in total and per torrent. Empty means no limit."),
                    "fields": [root.num("max_uploads", {
                                            "none": -1,
                                            "noneText": root.inf,
                                            "min": 1
                                        }), root.num("max_uploads_per_torrent", {
                                                         "none": -1,
                                                         "noneText": root.inf,
                                                         "min": 1,
                                                         "sep": "·"
                                                     })]
                },
                {
                    "glyph": "⌁",
                    "title": qsTr("Network interface"),
                    "desc": qsTr("Bind to one interface, a VPN's for example. Any uses them all."),
                    "pick": {
                        "key": "current_network_interface",
                        "source": "interfaces"
                    },
                    "confirm": "iface"
                },
                {
                    "title": qsTr("Address"),
                    "desc": qsTr("One address of that interface, or all of them."),
                    "sub": true,
                    "dep": "current_network_interface",
                    "pick": {
                        "key": "current_interface_address",
                        "source": "addresses"
                    },
                    "confirm": "iface"
                },
                {
                    "glyph": "⟳",
                    "title": qsTr("Reannounce when the address changes"),
                    "toggle": "reannounce_when_address_changed"
                },
                {
                    "glyph": "⋈",
                    "title": qsTr("Several connections from one address"),
                    "toggle": "enable_multi_connections_from_same_ip"
                },
                {
                    "glyph": "⊘",
                    "title": qsTr("IP filter"),
                    "desc": qsTr("Block the addresses in a filter file (.dat, .p2p or .p2b) on the server."),
                    "toggle": "ip_filter_enabled",
                    "confirm": "ipfilter"
                },
                {
                    "title": qsTr("Filter file"),
                    "sub": true,
                    "dep": "ip_filter_enabled",
                    "fields": [root.text("ip_filter_path", 220, qsTr("a path on the server"))],
                    "confirm": "ipfilter"
                },
                {
                    "title": qsTr("Apply to trackers too"),
                    "sub": true,
                    "dep": "ip_filter_enabled",
                    "toggle": "ip_filter_trackers"
                },
                {
                    "glyph": "⊘",
                    "title": qsTr("Banned addresses"),
                    "desc": qsTr("Peers you banned with b in the Peers tab, and any you add here."),
                    "list": {
                        "key": "banned_IPs",
                        "sep": "\n",
                        "what": qsTr("addresses")
                    }
                }
            ]
        },
        {
            "id": "proxy",
            "glyph": "⇢",
            "name": qsTr("Proxy"),
            "shortName": qsTr("Proxy"),
            "sub": qsTr("a SOCKS or HTTP proxy between qBittorrent and the rest"),
            "rows": [
                {
                    "glyph": "⇢",
                    "title": qsTr("Proxy"),
                    "choice": {
                        "key": "proxy_type",
                        "options": [["None", qsTr("none")], ["SOCKS4", "SOCKS4"], ["SOCKS5", "SOCKS5"], ["HTTP", "HTTP"]]
                    }
                },
                {
                    "title": qsTr("Host, port"),
                    "sub": true,
                    "dep": "proxy_type!=None",
                    "fields": [root.text("proxy_ip", 180, "proxy.example.org"), root.num("proxy_port", {
                                                                                        "min": 1,
                                                                                        "max": 65535,
                                                                                        "sep": ":"
                                                                                    })]
                },
                {
                    "title": qsTr("Sign in to the proxy"),
                    "sub": true,
                    "dep": "proxy_type!=None",
                    "toggle": "proxy_auth_enabled"
                },
                {
                    "title": qsTr("Username, password"),
                    "sub": true,
                    "dep": ["proxy_type!=None", "proxy_auth_enabled"],
                    "fields": [root.text("proxy_username", 130, ""), root.secret("proxy_password")]
                },
                {
                    "title": qsTr("Look up host names through the proxy"),
                    "sub": true,
                    "dep": "proxy_type!=None",
                    "toggle": "proxy_hostname_lookup"
                },
                {
                    "group": qsTr("use it for")
                },
                {
                    "glyph": "⇅",
                    "title": qsTr("BitTorrent"),
                    "desc": qsTr("Trackers and, with the row below, peers."),
                    "dep": "proxy_type!=None",
                    "toggle": "proxy_bittorrent"
                },
                {
                    "title": qsTr("Peer connections"),
                    "sub": true,
                    "dep": ["proxy_type!=None", "proxy_bittorrent"],
                    "toggle": "proxy_peer_connections"
                },
                {
                    "glyph": "◦",
                    "title": qsTr("RSS"),
                    "dep": "proxy_type!=None",
                    "toggle": "proxy_rss"
                },
                {
                    "glyph": "◦",
                    "title": qsTr("Everything else"),
                    "desc": qsTr("Search plugins, update checks and other lookups."),
                    "dep": "proxy_type!=None",
                    "toggle": "proxy_misc"
                }
            ]
        },
        {
            "id": "seeding",
            "glyph": "↑",
            "name": qsTr("Seeding"),
            "shortName": qsTr("Seeding"),
            "sub": qsTr("when a torrent stops seeding"),
            "rows": [
                {
                    "note": qsTr("A torrent's own limits (L, the Limits sheet) win over these. \"global\" there means the values here.")
                },
                {
                    "glyph": "÷",
                    "title": qsTr("Share ratio"),
                    "desc": qsTr("Stop once a torrent has uploaded this many times its size."),
                    "toggle": "max_ratio_enabled",
                    "enable": {
                        "max_ratio": 1
                    },
                    "fields": [root.num("max_ratio", {
                                            "decimals": 2,
                                            "min": 0,
                                            "none": -1,
                                            "noneText": root.inf
                                        })]
                },
                {
                    "glyph": "◷",
                    "title": qsTr("Seeding time"),
                    "desc": qsTr("Stop after seeding this long."),
                    "toggle": "max_seeding_time_enabled",
                    "enable": {
                        "max_seeding_time": 1440
                    },
                    "fields": [root.num("max_seeding_time", {
                                            "unit": qsTr("min"),
                                            "min": 0,
                                            "none": -1,
                                            "noneText": root.inf,
                                            "width": 100
                                        })]
                },
                {
                    "glyph": "◷",
                    "title": qsTr("Inactive seeding time"),
                    "desc": qsTr("Stop after this long with no upload."),
                    "toggle": "max_inactive_seeding_time_enabled",
                    "enable": {
                        "max_inactive_seeding_time": 1440
                    },
                    "fields": [root.num("max_inactive_seeding_time", {
                                            "unit": qsTr("min"),
                                            "min": 0,
                                            "none": -1,
                                            "noneText": root.inf,
                                            "width": 100
                                        })]
                },
                {
                    "glyph": "■",
                    "title": qsTr("At a limit"),
                    "desc": qsTr("What a torrent does when it reaches one of these."),
                    "choice": {
                        "key": "max_ratio_act",
                        "options": [[0, qsTr("stop")], [1, qsTr("remove")], [3, qsTr("remove with files")], [2, qsTr("super seed")]]
                    }
                }
            ]
        },
        {
            "id": "queue",
            "glyph": "≡",
            "name": qsTr("Queue"),
            "shortName": qsTr("Queue"),
            "sub": qsTr("how many run at once"),
            "rows": [
                {
                    "glyph": "≡",
                    "title": qsTr("Queue torrents"),
                    "desc": qsTr("Only a few torrents run at once; the rest wait their turn in the queue."),
                    "toggle": "queueing_enabled"
                },
                {
                    "title": qsTr("Active at once"),
                    "desc": qsTr("Downloads · uploads · in total. Empty means no limit."),
                    "sub": true,
                    "dep": "queueing_enabled",
                    "fields": [root.num("max_active_downloads", {
                                            "none": -1,
                                            "noneText": root.inf,
                                            "min": 0,
                                            "width": 56
                                        }), root.num("max_active_uploads", {
                                                         "none": -1,
                                                         "noneText": root.inf,
                                                         "min": 0,
                                                         "width": 56,
                                                         "sep": "·"
                                                     }), root.num("max_active_torrents", {
                                                                      "none": -1,
                                                                      "noneText": root.inf,
                                                                      "min": 0,
                                                                      "width": 56,
                                                                      "sep": "·"
                                                                  })]
                },
                {
                    "title": qsTr("Don't count slow torrents"),
                    "desc": qsTr("A torrent below these speeds for this long doesn't take a slot."),
                    "sub": true,
                    "dep": "queueing_enabled",
                    "toggle": "dont_count_slow_torrents"
                },
                {
                    "title": qsTr("Slow means"),
                    "desc": qsTr("Download · upload below, for this long."),
                    "sub": true,
                    "dep": ["queueing_enabled", "dont_count_slow_torrents"],
                    "fields": [root.num("slow_torrent_dl_rate_threshold", {
                                            "unit": qsTr("KB/s"),
                                            "min": 0,
                                            "width": 90
                                        }), root.num("slow_torrent_ul_rate_threshold", {
                                                         "unit": qsTr("KB/s"),
                                                         "min": 0,
                                                         "width": 90,
                                                         "sep": "·"
                                                     }), root.num("slow_torrent_inactive_timer", {
                                                                      "unit": qsTr("s"),
                                                                      "min": 1,
                                                                      "width": 70,
                                                                      "sep": "·"
                                                                  })]
                },
                {
                    "glyph": "↻",
                    "title": qsTr("Checking at once"),
                    "desc": qsTr("Torrents being rechecked at the same time."),
                    "fields": [root.num("max_active_checking_torrents", {
                                            "min": 1
                                        })]
                }
            ]
        },
        {
            "id": "downloads",
            "glyph": "▤",
            "name": qsTr("Downloads and files"),
            "shortName": qsTr("Downloads"),
            "sub": qsTr("where files go, and what happens on add"),
            "rows": [
                {
                    "group": qsTr("where")
                },
                {
                    "glyph": "▤",
                    "title": qsTr("Default save path"),
                    "desc": qsTr("A path on the server."),
                    "fields": [root.text("save_path", 240, "")]
                },
                {
                    "glyph": "▤",
                    "title": qsTr("Keep incomplete files elsewhere"),
                    "toggle": "temp_path_enabled",
                    "fields": [root.text("temp_path", 200, "")]
                },
                {
                    "glyph": "◈",
                    "title": qsTr("Automatic torrent management"),
                    "desc": qsTr("New torrents save to their category's folder."),
                    "toggle": "auto_tmm_enabled"
                },
                {
                    "title": qsTr("Move the files when a torrent's category changes"),
                    "sub": true,
                    "dep": "auto_tmm_enabled",
                    "toggle": "torrent_changed_tmm_enabled"
                },
                {
                    "title": qsTr("Move them when the default path changes"),
                    "sub": true,
                    "dep": "auto_tmm_enabled",
                    "toggle": "save_path_changed_tmm_enabled"
                },
                {
                    "title": qsTr("Move them when a category's path changes"),
                    "sub": true,
                    "dep": "auto_tmm_enabled",
                    "toggle": "category_changed_tmm_enabled"
                },
                {
                    "glyph": "◈",
                    "title": qsTr("Category folders without automatic management"),
                    "desc": qsTr("A manually managed torrent with a category still goes to that category's folder."),
                    "toggle": "use_category_paths_in_manual_mode"
                },
                {
                    "glyph": "▦",
                    "title": qsTr("Content layout"),
                    "choice": {
                        "key": "torrent_content_layout",
                        "options": [["Original", qsTr("original")], ["Subfolder", qsTr("subfolder")], ["NoSubfolder", qsTr("no subfolder")]]
                    }
                },
                {
                    "group": qsTr("adding")
                },
                {
                    "glyph": "■",
                    "title": qsTr("Add torrents stopped"),
                    "toggle": "add_stopped_enabled"
                },
                {
                    "glyph": "⤒",
                    "title": qsTr("Add to the top of the queue"),
                    "toggle": "add_to_top_of_queue"
                },
                {
                    "glyph": "■",
                    "title": qsTr("Stop when"),
                    "choice": {
                        "key": "torrent_stop_condition",
                        "options": [["None", qsTr("never")], ["MetadataReceived", qsTr("metadata received")], ["FilesChecked", qsTr("files checked")]]
                    }
                },
                {
                    "glyph": "⎘",
                    "title": qsTr("Merge trackers when a duplicate is added"),
                    "toggle": "merge_trackers"
                },
                {
                    "glyph": "⌫",
                    "title": qsTr("Delete the .torrent after adding"),
                    "choice": {
                        "key": "auto_delete_mode",
                        "options": [[0, qsTr("never")], [1, qsTr("if added")], [2, qsTr("always")]]
                    }
                },
                {
                    "glyph": "◫",
                    "title": qsTr("Watch folders"),
                    "desc": qsTr("Add every .torrent that appears in these folders on the server, saved to the default path."),
                    "list": {
                        "key": "scan_dirs",
                        "dirs": true,
                        "what": qsTr("folders")
                    }
                },
                {
                    "glyph": "⎘",
                    "title": qsTr("Copy .torrent files to"),
                    "desc": qsTr("All of them · finished ones only. Empty copies none."),
                    "failure": "export",
                    "fields": [root.text("export_dir", 120, "—"), {
                            "key": "export_dir_fin",
                            "kind": "text",
                            "width": 120,
                            "noneText": "—",
                            "sep": "·"
                        }]
                },
                {
                    "group": qsTr("files")
                },
                {
                    "glyph": "▥",
                    "title": qsTr("Preallocate disk space"),
                    "toggle": "preallocate_all"
                },
                {
                    "glyph": ".",
                    "title": qsTr("Mark incomplete files (.!qB)"),
                    "toggle": "incomplete_files_ext"
                },
                {
                    "glyph": ".",
                    "title": qsTr("Keep unselected files in .unwanted"),
                    "toggle": "use_unwanted_folder"
                },
                {
                    "glyph": "⊘",
                    "title": qsTr("Skip files named"),
                    "desc": qsTr("One pattern per line, like *.nfo or sample.*"),
                    "toggle": "excluded_file_names_enabled",
                    "list": {
                        "key": "excluded_file_names",
                        "sep": "\n",
                        "what": qsTr("patterns")
                    }
                },
                {
                    "glyph": "✓",
                    "title": qsTr("Recheck finished torrents"),
                    "toggle": "recheck_completed_torrents"
                },
                {
                    "glyph": "⌫",
                    "title": qsTr("Deleted files"),
                    "desc": qsTr("When a torrent is deleted with its files."),
                    "choice": {
                        "key": "torrent_content_remove_option",
                        "options": [["Delete", qsTr("delete them")], ["MoveToTrash", qsTr("move to trash")]]
                    }
                },
                {
                    "group": qsTr("run a program")
                },
                {
                    "glyph": "▸",
                    "title": qsTr("When a torrent is added"),
                    "desc": qsTr("%N name · %L category · %F content path · %I hash…"),
                    "toggle": "autorun_on_torrent_added_enabled",
                    "fields": [root.text("autorun_on_torrent_added_program", 200, qsTr("command"))]
                },
                {
                    "glyph": "▸",
                    "title": qsTr("When a torrent finishes"),
                    "toggle": "autorun_enabled",
                    "fields": [root.text("autorun_program", 200, qsTr("command"))]
                },
                {
                    "group": qsTr("email when a torrent finishes")
                },
                {
                    "glyph": "✉",
                    "title": qsTr("Send an email"),
                    "desc": qsTr("omaseed already tells the desktop; this is qBittorrent's own."),
                    "toggle": "mail_notification_enabled"
                },
                {
                    "title": qsTr("From, to"),
                    "sub": true,
                    "dep": "mail_notification_enabled",
                    "fields": [root.text("mail_notification_sender", 160, ""), {
                            "key": "mail_notification_email",
                            "kind": "text",
                            "width": 160,
                            "sep": "→"
                        }]
                },
                {
                    "title": qsTr("SMTP server"),
                    "sub": true,
                    "dep": "mail_notification_enabled",
                    "fields": [root.text("mail_notification_smtp", 200, "")]
                },
                // qBittorrent 5.2 has the switch; 5.3 (Web API 2.16.0) replaced it with the
                // encryption type. A server shows the one it has (QbtSettingsPane's `rows`).
                {
                    "title": qsTr("Use SSL"),
                    "sub": true,
                    "dep": "mail_notification_enabled",
                    "toggle": "mail_notification_ssl_enabled"
                },
                {
                    "title": qsTr("Encryption"),
                    "desc": qsTr("SSL from the start (SMTPS, often port 465), or after a plain hello (STARTTLS, often 587)."),
                    "sub": true,
                    "dep": "mail_notification_enabled",
                    "choice": {
                        "key": "mail_notification_encryption_type",
                        "options": [["SMTPS", "SMTPS"], ["STARTTLS", "STARTTLS"], ["None", qsTr("none")]]
                    }
                },
                {
                    "title": qsTr("Sign in"),
                    "sub": true,
                    "dep": "mail_notification_enabled",
                    "toggle": "mail_notification_auth_enabled"
                },
                {
                    "title": qsTr("Username, password"),
                    "sub": true,
                    "dep": ["mail_notification_enabled", "mail_notification_auth_enabled"],
                    "fields": [root.text("mail_notification_username", 130, ""), root.secret("mail_notification_password")]
                }
            ]
        },
        {
            "id": "webui",
            "glyph": "⌘",
            "name": qsTr("Web UI"),
            "shortName": qsTr("Web UI"),
            "sub": qsTr("the door omaseed comes in by"),
            "webui": true,
            "rows": [
                {
                    "note": qsTr("omaseed signs in through the Web UI, so every change here asks first. A new port, HTTPS or login is followed by omaseed's own saved server."),
                    "warn": true
                },
                {
                    "glyph": "⊙",
                    "title": qsTr("Address, port"),
                    "desc": qsTr("* listens on every address."),
                    "fields": [root.text("web_ui_address", 150, "*"), root.num("web_ui_port", {
                                                                              "min": 1,
                                                                              "max": 65535,
                                                                              "sep": ":"
                                                                          })]
                },
                {
                    "glyph": "⌬",
                    "title": qsTr("Forward the port with UPnP"),
                    "toggle": "web_ui_upnp"
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("HTTPS"),
                    "toggle": "use_https",
                    // Without both, qBittorrent stays on HTTP but marks its login cookie
                    // secure, which nothing sends over HTTP: every new login is shut out.
                    "needs": {
                        "keys": ["web_ui_https_cert_path", "web_ui_https_key_path"],
                        "note": qsTr("set the certificate and key first")
                    }
                },
                {
                    "title": qsTr("Certificate, key"),
                    "desc": qsTr("Paths on the server. Set them before turning HTTPS on."),
                    "sub": true,
                    "fields": [root.text("web_ui_https_cert_path", 150, ""), {
                            "key": "web_ui_https_key_path",
                            "kind": "text",
                            "width": 150,
                            "sep": "·"
                        }]
                },
                {
                    "group": qsTr("login")
                },
                {
                    "glyph": "◉",
                    "title": qsTr("Username"),
                    "fields": [root.text("web_ui_username", 150, "")]
                },
                {
                    "glyph": "◉",
                    "title": qsTr("New password"),
                    "desc": qsTr("At least 6 characters. qBittorrent never shows the old one."),
                    "fields": [root.secret("web_ui_password")]
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("API key"),
                    "desc": qsTr("A new key replaces the old one. If omaseed signs in with a key, it takes the new one."),
                    "button": {
                        "text": qsTr("new key"),
                        "action": "rotateKey"
                    }
                },
                {
                    "glyph": "⌂",
                    "title": qsTr("Skip the login on this computer"),
                    "desc": qsTr("Requests from the server's own machine need no login."),
                    "toggle": "bypass_local_auth"
                },
                {
                    "glyph": "⌂",
                    "title": qsTr("Skip the login from these networks"),
                    "toggle": "bypass_auth_subnet_whitelist_enabled"
                },
                {
                    "title": qsTr("Networks"),
                    "sub": true,
                    "dep": "bypass_auth_subnet_whitelist_enabled",
                    "list": {
                        "key": "bypass_auth_subnet_whitelist",
                        "sep": "\n",
                        "what": qsTr("networks, like 192.168.1.0/24")
                    }
                },
                {
                    "glyph": "⊘",
                    "title": qsTr("Ban after failed logins"),
                    "desc": qsTr("Failed logins · then banned for."),
                    "fields": [root.num("web_ui_max_auth_fail_count", {
                                            "min": 0,
                                            "width": 56
                                        }), root.num("web_ui_ban_duration", {
                                                         "unit": qsTr("s"),
                                                         "min": 1,
                                                         "width": 90,
                                                         "sep": "·"
                                                     })]
                },
                {
                    "glyph": "◷",
                    "title": qsTr("Session timeout"),
                    "fields": [root.num("web_ui_session_timeout", {
                                            "unit": qsTr("s"),
                                            "min": 0,
                                            "width": 90
                                        })]
                },
                {
                    "group": qsTr("security")
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("Clickjacking protection"),
                    "toggle": "web_ui_clickjacking_protection_enabled"
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("CSRF protection"),
                    "toggle": "web_ui_csrf_protection_enabled"
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("Secure cookie"),
                    "desc": qsTr("Only over HTTPS."),
                    "toggle": "web_ui_secure_cookie_enabled"
                },
                {
                    "glyph": "⚿",
                    "title": qsTr("Check the Host header"),
                    "toggle": "web_ui_host_header_validation_enabled"
                },
                {
                    "title": qsTr("Allowed domains"),
                    "sub": true,
                    "dep": "web_ui_host_header_validation_enabled",
                    "list": {
                        "key": "web_ui_domain_list",
                        "sep": ";",
                        "what": qsTr("domains, * for any")
                    }
                },
                {
                    "glyph": "≡",
                    "title": qsTr("Add HTTP headers"),
                    "toggle": "web_ui_use_custom_http_headers_enabled"
                },
                {
                    "title": qsTr("Headers"),
                    "sub": true,
                    "dep": "web_ui_use_custom_http_headers_enabled",
                    "list": {
                        "key": "web_ui_custom_http_headers",
                        "sep": "\n",
                        "what": qsTr("headers, like Name: value")
                    }
                },
                {
                    "glyph": "⇢",
                    "title": qsTr("Behind a reverse proxy"),
                    "toggle": "web_ui_reverse_proxy_enabled"
                },
                {
                    "title": qsTr("Trusted proxies"),
                    "sub": true,
                    "dep": "web_ui_reverse_proxy_enabled",
                    "list": {
                        "key": "web_ui_reverse_proxies_list",
                        "sep": ";",
                        "what": qsTr("proxy addresses")
                    }
                },
                {
                    "glyph": "◫",
                    "title": qsTr("Another web UI"),
                    "desc": qsTr("Serve a different web UI from a folder on the server."),
                    "toggle": "alternative_webui_enabled",
                    "fields": [root.text("alternative_webui_path", 180, "")]
                },
                {
                    "group": qsTr("dynamic DNS")
                },
                {
                    "glyph": "◎",
                    "title": qsTr("Keep a name pointed at the server"),
                    "toggle": "dyndns_enabled"
                },
                {
                    "title": qsTr("Service"),
                    "sub": true,
                    "dep": "dyndns_enabled",
                    "choice": {
                        "key": "dyndns_service",
                        "options": [[0, "DynDNS"], [1, "No-IP"]]
                    }
                },
                {
                    "title": qsTr("Domain"),
                    "sub": true,
                    "dep": "dyndns_enabled",
                    "fields": [root.text("dyndns_domain", 200, "")]
                },
                {
                    "title": qsTr("Username, password"),
                    "sub": true,
                    "dep": "dyndns_enabled",
                    "fields": [root.text("dyndns_username", 130, ""), root.secret("dyndns_password")]
                }
            ]
        },
        {
            "id": "advanced",
            "glyph": "◈",
            "name": qsTr("Advanced"),
            "shortName": qsTr("Advanced"),
            "sub": qsTr("libtorrent tuning"),
            "rows": [
                {
                    "note": qsTr("These tune libtorrent, the engine inside qBittorrent. The defaults suit almost everyone. Each changed row shows default to put it back, and d does the same."),
                    "warn": true
                },
                {
                    "group": qsTr("disk")
                },
                root.adv("▥", qsTr("Disk cache"), qsTr("Empty lets libtorrent decide."), {
                             "fields": [root.num("disk_cache", {
                                                     "unit": qsTr("MiB"),
                                                     "none": -1,
                                                     "noneText": qsTr("auto"),
                                                     "min": 0,
                                                     "width": 100
                                                 })]
                         }),
                root.adv("▥", qsTr("Disk cache expiry"), "", {
                             "fields": [root.num("disk_cache_ttl", {
                                                     "unit": qsTr("s"),
                                                     "min": 1,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("▥", qsTr("Disk I/O"), "", {
                             "choice": {
                                 "key": "disk_io_type",
                                 "options": [[0, qsTr("default")], [1, qsTr("memory-mapped")], [2, qsTr("POSIX")]]
                             }
                         }),
                root.adv("▥", qsTr("Reads"), qsTr("Through the system's file cache, or straight to the disk."), {
                             "choice": {
                                 "key": "disk_io_read_mode",
                                 "options": [[1, qsTr("cached")], [0, qsTr("direct")]]
                             }
                         }),
                root.adv("▥", qsTr("Writes"), "", {
                             "choice": {
                                 "key": "disk_io_write_mode",
                                 "options": [[1, qsTr("cached")], [0, qsTr("direct")], [2, qsTr("write-through")]]
                             }
                         }),
                root.adv("▥", qsTr("Disk queue"), qsTr("Data waiting to be written, at most."), {
                             "fields": [root.num("disk_queue_size", {
                                                     "unit": qsTr("KiB"),
                                                     "scale": 1024,
                                                     "min": 1,
                                                     "width": 110
                                                 })]
                         }),
                root.adv("▥", qsTr("I/O threads · hashing threads"), "", {
                             "fields": [root.num("async_io_threads", {
                                                     "min": 1,
                                                     "width": 56
                                                 }), root.num("hashing_threads", {
                                                                  "min": 1,
                                                                  "width": 56,
                                                                  "sep": "·"
                                                              })]
                         }),
                root.adv("▥", qsTr("Open files at once"), "", {
                             "fields": [root.num("file_pool_size", {
                                                     "min": 1
                                                 })]
                         }),
                root.adv("▥", qsTr("Memory for checking"), "", {
                             "fields": [root.num("checking_memory_use", {
                                                     "unit": qsTr("MiB"),
                                                     "min": 1,
                                                     "width": 100
                                                 })]
                         }),
                root.adv("▥", qsTr("Memory limit"), qsTr("For qBittorrent's working set. Windows only; others ignore it."), {
                             "fields": [root.num("memory_working_set_limit", {
                                                     "unit": qsTr("MiB"),
                                                     "min": 1,
                                                     "width": 100
                                                 })]
                         }),
                root.adv("▥", qsTr("Coalesce reads and writes"), "", {
                             "toggle": "enable_coalesce_read_write"
                         }),
                root.adv("▥", qsTr("Piece extent affinity"), qsTr("Prefer whole blocks of pieces from each peer."), {
                             "toggle": "enable_piece_extent_affinity"
                         }),
                root.adv("▥", qsTr("Send upload suggestions"), "", {
                             "toggle": "enable_upload_suggestions"
                         }),
                root.adv("▥", qsTr("Save resume data every"), "", {
                             "fields": [root.num("save_resume_data_interval", {
                                                     "unit": qsTr("min"),
                                                     "min": 0,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("▥", qsTr("Save statistics every"), "", {
                             "fields": [root.num("save_statistics_interval", {
                                                     "unit": qsTr("min"),
                                                     "min": 0,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("▥", qsTr("Resume data kept in"), qsTr("Takes effect after qBittorrent restarts."), {
                             "choice": {
                                 "key": "resume_data_storage_type",
                                 "options": [["Legacy", qsTr("files")], ["SQLite", qsTr("a database")]]
                             }
                         }),
                root.adv("▥", qsTr(".torrent files up to"), "", {
                             "fields": [root.num("torrent_file_size_limit", {
                                                     "unit": qsTr("MiB"),
                                                     "scale": 1048576,
                                                     "min": 1,
                                                     "width": 100
                                                 })]
                         }),
                root.adv("▥", qsTr("Bdecode depth · token limit"), qsTr("How deep and how long a .torrent's data may be."), {
                             "fields": [root.num("bdecode_depth_limit", {
                                                     "min": 1,
                                                     "width": 70
                                                 }), root.num("bdecode_token_limit", {
                                                                  "min": 1,
                                                                  "width": 110,
                                                                  "sep": "·"
                                                              })]
                         }),
                root.adv("▥", qsTr("Mark downloaded files as from the internet"), qsTr("Windows and macOS only."), {
                             "toggle": "mark_of_the_web"
                         }),
                {
                    "group": qsTr("network")
                },
                root.adv("⊙", qsTr("Outgoing ports"), qsTr("0 and 0 let the system choose."), {
                             "fields": [root.num("outgoing_ports_min", {
                                                     "min": 0,
                                                     "max": 65535
                                                 }), root.num("outgoing_ports_max", {
                                                                  "min": 0,
                                                                  "max": 65535,
                                                                  "sep": "–"
                                                              })]
                         }),
                root.adv("⇄", qsTr("µTP beside TCP"), "", {
                             "choice": {
                                 "key": "utp_tcp_mixed_mode",
                                 "options": [[0, qsTr("prefer TCP")], [1, qsTr("in proportion")]]
                             }
                         }),
                root.adv("⌬", qsTr("UPnP lease"), qsTr("0 asks for a permanent one."), {
                             "fields": [root.num("upnp_lease_duration", {
                                                     "unit": qsTr("s"),
                                                     "min": 0,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("⋈", qsTr("Type of service for peers"), qsTr("The DSCP byte on peer traffic."), {
                             "fields": [root.num("peer_tos", {
                                                     "min": 0,
                                                     "max": 255
                                                 })]
                         }),
                root.adv("⋈", qsTr("New connections per second"), "", {
                             "fields": [root.num("connection_speed", {
                                                     "min": 0
                                                 })]
                         }),
                root.adv("↑", qsTr("Upload slots"), "", {
                             "choice": {
                                 "key": "upload_slots_behavior",
                                 "options": [[0, qsTr("fixed")], [1, qsTr("by upload rate")]]
                             }
                         }),
                root.adv("↑", qsTr("Choking"), "", {
                             "choice": {
                                 "key": "upload_choking_algorithm",
                                 "options": [[0, qsTr("round-robin")], [1, qsTr("fastest upload")], [2, qsTr("anti-leech")]]
                             }
                         }),
                root.adv("⋈", qsTr("Peer turnover"), qsTr("Drop this share of peers · when below this share of the limit · every."), {
                             "fields": [root.num("peer_turnover", {
                                                     "unit": "%",
                                                     "min": 0,
                                                     "max": 100,
                                                     "width": 60
                                                 }), root.num("peer_turnover_cutoff", {
                                                                  "unit": "%",
                                                                  "min": 0,
                                                                  "max": 100,
                                                                  "width": 60,
                                                                  "sep": "·"
                                                              }), root.num("peer_turnover_interval", {
                                                                               "unit": qsTr("s"),
                                                                               "min": 0,
                                                                               "width": 76,
                                                                               "sep": "·"
                                                                           })]
                         }),
                root.adv("⋈", qsTr("Requests waiting per peer"), "", {
                             "fields": [root.num("request_queue_size", {
                                                     "min": 1
                                                 })]
                         }),
                root.adv("↑", qsTr("Send buffer"), qsTr("Watermark · low watermark · factor."), {
                             "fields": [root.num("send_buffer_watermark", {
                                                     "unit": qsTr("KiB"),
                                                     "min": 1,
                                                     "width": 80
                                                 }), root.num("send_buffer_low_watermark", {
                                                                  "unit": qsTr("KiB"),
                                                                  "min": 1,
                                                                  "width": 80,
                                                                  "sep": "·"
                                                              }), root.num("send_buffer_watermark_factor", {
                                                                               "unit": "%",
                                                                               "min": 1,
                                                                               "width": 64,
                                                                               "sep": "·"
                                                                           })]
                         }),
                root.adv("⋈", qsTr("Socket backlog"), "", {
                             "fields": [root.num("socket_backlog_size", {
                                                     "min": 1
                                                 })]
                         }),
                root.adv("⋈", qsTr("Socket send · receive buffer"), qsTr("0 leaves them to the system."), {
                             "fields": [root.num("socket_send_buffer_size", {
                                                     "unit": qsTr("KiB"),
                                                     "scale": 1024,
                                                     "min": 0,
                                                     "width": 90
                                                 }), root.num("socket_receive_buffer_size", {
                                                                  "unit": qsTr("KiB"),
                                                                  "scale": 1024,
                                                                  "min": 0,
                                                                  "width": 90,
                                                                  "sep": "·"
                                                              })]
                         }),
                {
                    "group": qsTr("trackers")
                },
                root.adv("↗", qsTr("Announce IP · port"), qsTr("What trackers are told, when it differs from what they see. 0 uses the listening port."), {
                             "fields": [root.text("announce_ip", 150, qsTr("auto")), root.num("announce_port", {
                                                                                               "min": 0,
                                                                                               "max": 65535,
                                                                                               "sep": ":"
                                                                                           })]
                         }),
                root.adv("↗", qsTr("Tracker announces at once"), "", {
                             "fields": [root.num("max_concurrent_http_announces", {
                                                     "min": 1
                                                 })]
                         }),
                root.adv("↗", qsTr("Stop announce timeout"), "", {
                             "fields": [root.num("stop_tracker_timeout", {
                                                     "unit": qsTr("s"),
                                                     "min": 0,
                                                     "width": 80
                                                 })]
                         }),
                root.adv("⚿", qsTr("Check tracker HTTPS certificates"), "", {
                             "toggle": "validate_https_tracker_certificate"
                         }),
                root.adv("⚿", qsTr("Ignore SSL errors"), qsTr("For downloads by URL and RSS."), {
                             "toggle": "ignore_ssl_errors"
                         }),
                root.adv("⊘", qsTr("Guard against request forgery"), qsTr("Refuse trackers and web seeds that point into your own network."), {
                             "toggle": "ssrf_mitigation"
                         }),
                root.adv("⊘", qsTr("Block peers on privileged ports"), "", {
                             "toggle": "block_peers_on_privileged_ports"
                         }),
                root.adv("◎", qsTr("International domain names"), "", {
                             "toggle": "idn_support_enabled"
                         }),
                root.adv("◷", qsTr("Host name cache"), "", {
                             "fields": [root.num("hostname_cache_ttl", {
                                                     "unit": qsTr("s"),
                                                     "min": 0,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("◎", qsTr("Show peers' countries"), "", {
                             "toggle": "resolve_peer_countries"
                         }),
                root.adv("◎", qsTr("Look up peers' host names"), "", {
                             "toggle": "resolve_peer_host_names"
                         }),
                root.adv("↗", qsTr("Built-in tracker"), qsTr("qBittorrent answers announces itself."), {
                             "toggle": "enable_embedded_tracker"
                         }),
                root.adv("", qsTr("Tracker port"), "", {
                             "sub": true,
                             "dep": "enable_embedded_tracker",
                             "fields": [root.num("embedded_tracker_port", {
                                                     "min": 1,
                                                     "max": 65535
                                                 })]
                         }),
                root.adv("", qsTr("Forward the tracker port"), "", {
                             "sub": true,
                             "dep": "enable_embedded_tracker",
                             "toggle": "embedded_tracker_port_forwarding"
                         }),
                root.adv("⚿", qsTr("SSL torrents"), qsTr("Accept peers of torrents that use SSL."), {
                             "toggle": "ssl_enabled"
                         }),
                {
                    "title": qsTr("SSL port"),
                    "sub": true,
                    "dep": "ssl_enabled",
                    "fields": [root.num("ssl_listen_port", {
                                            "min": 1,
                                            "max": 65535
                                        })]
                },
                {
                    "group": qsTr("I2P")
                },
                root.adv("◎", qsTr("I2P"), qsTr("Reach I2P torrents through a router on the server."), {
                             "toggle": "i2p_enabled"
                         }),
                root.adv("", qsTr("Router host, port"), "", {
                             "sub": true,
                             "dep": "i2p_enabled",
                             "fields": [root.text("i2p_address", 130, ""), root.num("i2p_port", {
                                                                                         "min": 1,
                                                                                         "max": 65535,
                                                                                         "sep": ":"
                                                                                     })]
                         }),
                root.adv("", qsTr("Mixed mode"), qsTr("I2P and ordinary peers in the same torrent."), {
                             "sub": true,
                             "dep": "i2p_enabled",
                             "toggle": "i2p_mixed_mode"
                         }),
                root.adv("", qsTr("Inbound length · quantity"), "", {
                             "sub": true,
                             "dep": "i2p_enabled",
                             "fields": [root.num("i2p_inbound_length", {
                                                     "min": 0,
                                                     "max": 7,
                                                     "width": 50
                                                 }), root.num("i2p_inbound_quantity", {
                                                                  "min": 1,
                                                                  "max": 16,
                                                                  "width": 50,
                                                                  "sep": "·"
                                                              })]
                         }),
                root.adv("", qsTr("Outbound length · quantity"), "", {
                             "sub": true,
                             "dep": "i2p_enabled",
                             "fields": [root.num("i2p_outbound_length", {
                                                     "min": 0,
                                                     "max": 7,
                                                     "width": 50
                                                 }), root.num("i2p_outbound_quantity", {
                                                                  "min": 1,
                                                                  "max": 16,
                                                                  "width": 50,
                                                                  "sep": "·"
                                                              })]
                         }),
                {
                    "group": qsTr("log file")
                },
                root.adv("≡", qsTr("Write a log file"), "", {
                             "toggle": "file_log_enabled"
                         }),
                {
                    "title": qsTr("Folder"),
                    "desc": qsTr("A path on the server."),
                    "sub": true,
                    "dep": "file_log_enabled",
                    "fields": [root.text("file_log_path", 240, "")]
                },
                root.adv("", qsTr("Start a new file past"), "", {
                             "sub": true,
                             "dep": "file_log_enabled",
                             "toggle": "file_log_backup_enabled",
                             "fields": [root.num("file_log_max_size", {
                                                     "unit": qsTr("KiB"),
                                                     "min": 1,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("", qsTr("Delete old files after"), "", {
                             "sub": true,
                             "dep": "file_log_enabled",
                             "toggle": "file_log_delete_old",
                             "fields": [root.num("file_log_age", {
                                                     "min": 1,
                                                     "width": 56
                                                 })],
                             "choice": {
                                 "key": "file_log_age_type",
                                 "options": [[0, qsTr("days")], [1, qsTr("months")], [2, qsTr("years")]]
                             }
                         }),
                {
                    "group": qsTr("other")
                },
                root.adv("◷", qsTr("Refresh interval"), qsTr("How often qBittorrent's own screens update."), {
                             "fields": [root.num("refresh_interval", {
                                                     "unit": qsTr("ms"),
                                                     "min": 30,
                                                     "width": 90
                                                 })]
                         }),
                root.adv("!", qsTr("Log performance warnings"), "", {
                             "toggle": "performance_warning"
                         }),
                {
                    "glyph": "◦",
                    "title": qsTr("Instance name"),
                    "desc": qsTr("Shown in qBittorrent's own window title."),
                    "fields": [root.text("app_instance_name", 160, "")]
                }
            ]
        }
    ]
}
