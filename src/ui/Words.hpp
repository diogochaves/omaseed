#pragma once

// Words — the runner for `omaseed WORD …` (DECISIONS C45 V2, V4, V5, V13; C37 U10): main()
// hands it a command line whose first argument is a word, before Qt's parser and before any
// display is needed. It parses (core::AgentWords), prints a page, or asks the running omaseed
// (integration::AgentClient) and prints its answer, then exits with V13's code.
//
// With --json, stdout carries exactly one JSON object: the app's answer as it sent it, or a
// failure object (the CLI's own have no "omaseed" key). Without it, a failure's words go to
// stderr.

#include <QString>

#include <functional>

namespace omaseed::ui {

/// `qtHelp` gives Qt's help text for omaseed's options (CommandLine in main.cpp), which the
/// help page splices; it is called with a QCoreApplication alive.
int runWord(int argc, char *argv[], const std::function<QString()> &qtHelp);

} // namespace omaseed::ui
