pragma Singleton
import QtQuick

// The matcher every palette filters with (spec v2 §4: "type to filter"). Deliberately small
// and predictable rather than clever: a candidate matches when, for every whitespace-separated
// word of the query, the word is either a plain substring of the text or appears in it as a
// subsequence (the letters in order, gaps allowed — `fwl` finds `Fedora-Workstation-Live`).
// Case is ignored. An empty query matches everything.
//
// `score` exists so a caller can sort its own filtered list: lower is better, a substring hit
// beats a subsequence hit, and an earlier hit beats a later one. Filtering and sorting stay
// the caller's job; `Palette` only draws what it is given.
QtObject {
    id: root

    /// The query's words, lower-cased, empties dropped.
    function words(query: string): list<string> {
        return String(query === undefined || query === null ? "" : query).toLowerCase().split(
                    /\s+/).filter(w => w.length > 0);
    }

    /// True when `text` contains `word`'s letters in order (gaps allowed).
    function subsequence(text: string, word: string): bool {
        let at = 0;
        for (let i = 0; i < word.length; ++i) {
            at = text.indexOf(word.charAt(i), at);
            if (at < 0)
                return false;
            at += 1;
        }
        return true;
    }

    /// True when every word of `query` hits `text` as a substring or a subsequence.
    function match(text: string, query: string): bool {
        const haystack = String(text === undefined || text === null ? "" : text).toLowerCase();
        const parts = root.words(query);
        for (let i = 0; i < parts.length; ++i) {
            if (haystack.indexOf(parts[i]) < 0 && !root.subsequence(haystack, parts[i]))
                return false;
        }
        return true;
    }

    /// How well `text` matches: -1 for no match, else the sum of each word's cost (the index
    /// of a substring hit, or 1000 + the span of a subsequence hit). Lower is better.
    function score(text: string, query: string): int {
        const haystack = String(text === undefined || text === null ? "" : text).toLowerCase();
        const parts = root.words(query);
        let total = 0;
        for (let i = 0; i < parts.length; ++i) {
            const at = haystack.indexOf(parts[i]);
            if (at >= 0) {
                total += at;
                continue;
            }
            if (!root.subsequence(haystack, parts[i]))
                return -1;
            total += 1000 + haystack.length;
        }
        return total;
    }
}
