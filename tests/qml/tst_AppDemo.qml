import QtQuick
import QtTest
import Omaseed

// The engine-owned fallback App of the test harness has no server until a test loads the
// demo library; every view test starts from here.
TestCase {
    name: "AppDemo"

    function test_load_demo_fills_the_models() {
        compare(App.hasServer, false);
        App.loadDemo(16);
        compare(App.demoMode, true);
        compare(App.torrents.count, 16);
        compare(App.filtered.count, 16);
        compare(App.counts.downloading, 6);
    }
}
