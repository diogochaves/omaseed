pragma Singleton
import QtQuick

// The matcher every palette filters with (spec v2 §4: "type to filter"). Deliberately small
// and predictable rather than clever: a candidate matches when, for every whitespace-separated
// word of the query, the word is either a plain substring of the text or appears in it as a
// subsequence (the letters in order, gaps allowed — `fwl` finds `Fedora-Workstation-Live`).
// Case and accents are ignored, and so is the Unicode form: both sides are folded (`fold`), so
// `cafe` finds `Café` and a composed query finds a decomposed name (macOS writes NFD). An empty
// query matches everything.
//
// `score` exists so a caller can sort its own filtered list: lower is better, a substring hit
// beats a subsequence hit, an earlier hit beats a later one, and a tighter subsequence beats
// a looser one. Filtering and sorting stay the caller's job; `Palette` only draws what it is
// given.
QtObject {
    id: root

    /// `text` lower-cased, in compatibility-decomposed form, with its combining marks dropped.
    function fold(text: var): string {
        return String(text === undefined || text === null ? "" : text).normalize("NFKD").replace(
                    /[̀-ͯ]/g, "").toLowerCase();
    }

    /// The query's words, folded, empties dropped.
    function words(query: string): list<string> {
        return root.fold(query).split(/\s+/).filter(w => w.length > 0);
    }

    /// True when `text` contains `word`'s letters in order (gaps allowed).
    function subsequence(text: string, word: string): bool {
        return root._span(text, word) >= 0;
    }

    /// The span, first matched letter to last, of the leftmost in-order hit of `word`'s
    /// letters in `text`, or -1 when there is none.
    function _span(text: string, word: string): int {
        let at = 0;
        let first = -1;
        for (let i = 0; i < word.length; ++i) {
            at = text.indexOf(word.charAt(i), at);
            if (at < 0)
                return -1;
            if (first < 0)
                first = at;
            at += 1;
        }
        return first < 0 ? 0 : at - first;
    }

    /// True when every word of `query` hits `text` as a substring or a subsequence.
    function match(text: string, query: string): bool {
        const haystack = root.fold(text);
        const parts = root.words(query);
        for (let i = 0; i < parts.length; ++i) {
            if (haystack.indexOf(parts[i]) < 0 && !root.subsequence(haystack, parts[i]))
                return false;
        }
        return true;
    }

    /// How well `text` matches: -1 for no match, else the sum of each word's cost (the index
    /// of a substring hit, at most 999, or 1000 + the span of a subsequence hit). Lower is
    /// better.
    function score(text: string, query: string): int {
        const haystack = root.fold(text);
        const parts = root.words(query);
        let total = 0;
        for (let i = 0; i < parts.length; ++i) {
            const at = haystack.indexOf(parts[i]);
            if (at >= 0) {
                total += Math.min(at, 999);
                continue;
            }
            const span = root._span(haystack, parts[i]);
            if (span < 0)
                return -1;
            total += 1000 + span;
        }
        return total;
    }
}
