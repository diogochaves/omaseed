import QtQuick
import QtTest
import Omaseed.Kit

// T4.1. `Kit/Fuzzy.qml`, the matcher every palette filters with (spec v2 §4 "type to filter").
TestCase {
    id: testCase

    name: "Fuzzy"

    function test_an_empty_query_matches_everything() {
        verify(Fuzzy.match("Fedora-Workstation-Live-44-1.6.x86_64.iso", ""));
        verify(Fuzzy.match("", ""));
        verify(Fuzzy.match("anything", "   "));
    }

    function test_a_substring_matches_whatever_the_case() {
        verify(Fuzzy.match("Fedora-Workstation-Live-44", "fed"));
        verify(Fuzzy.match("Fedora-Workstation-Live-44", "WORKSTATION"));
        verify(!Fuzzy.match("Fedora-Workstation-Live-44", "ubuntu"));
    }

    function test_a_subsequence_matches_with_gaps() {
        verify(Fuzzy.match("Fedora-Workstation-Live-44", "fwl"));
        verify(!Fuzzy.match("Fedora-Workstation-Live-44", "lwf"));
    }

    function test_every_word_of_the_query_must_hit() {
        verify(Fuzzy.match("debian-13.1.0-amd64-DVD-1.iso", "deb dvd"));
        verify(!Fuzzy.match("debian-13.1.0-amd64-DVD-1.iso", "deb ubuntu"));
    }

    function test_score_prefers_the_earlier_substring() {
        const early = Fuzzy.score("linux mint", "linux");
        const late = Fuzzy.score("arch linux", "linux");
        verify(early >= 0 && late >= 0);
        verify(early < late, "an earlier hit should score lower");
    }

    function test_score_is_negative_when_nothing_matches() {
        compare(Fuzzy.score("Fedora", "ubuntu"), -1);
    }

    function test_score_ranks_a_substring_above_a_subsequence() {
        verify(Fuzzy.score("Fedora-Workstation", "work") < Fuzzy.score("Fedora-Workstation",
                                                                       "fwn"));
    }

    function test_words_splits_on_whitespace() {
        compare(Fuzzy.words("  Deb   DVD "), ["deb", "dvd"]);
        compare(Fuzzy.words(""), []);
    }

    // KIT-10: accents and the Unicode form do not stop a match.
    function test_accents_are_ignored_both_ways() {
        verify(Fuzzy.match("Café del Mar", "cafe"));
        verify(Fuzzy.match("Cafe del Mar", "café"));
        verify(Fuzzy.match("Łódź-2026", "odz"));
        verify(!Fuzzy.match("Café", "cafx"));
    }

    function test_a_composed_query_finds_a_decomposed_name() {
        const nfd = "Cafe\u0301"; // what macOS writes
        const nfc = "Caf\u00e9";
        verify(Fuzzy.match(nfd, nfc));
        verify(Fuzzy.match(nfc, nfd));
        compare(Fuzzy.score(nfd, nfc), 0);
    }

    function test_a_tighter_subsequence_scores_better() {
        verify(Fuzzy.score("axxxxbxc", "abc") > Fuzzy.score("xxxxxxxxxxxxxxxxxxxab-c", "abc"),
               "a looser hit ranked above a tighter one");
        compare(Fuzzy.score("a-b-c", "abc"), 1005);
    }

    function test_a_late_substring_still_beats_any_subsequence() {
        const late = "x".repeat(1500) + "fedora";
        verify(Fuzzy.score(late, "fedora") < Fuzzy.score("f-e-d-o-r-a", "fedora"));
    }
}
