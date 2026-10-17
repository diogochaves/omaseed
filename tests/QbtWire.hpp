#pragma once

// QbtWire — qBittorrent's transport (QbtClient) for the tests that need the wire itself.
//
// Everything in src/ reaches a server through EngineClient, and only src/core/engines/qbt/
// includes QbtClient.hpp (docs/DECISIONS.md C14, #82). Two kinds of test reach past that,
// through this header and nowhere else:
//   * the transport's own tests (tst_qbtclient_*): auth, retries, TLS, each endpoint's form;
//   * live tests that read or set a qBittorrent behind the app's back, as a witness that does
//     not share the code under test (what landed on the server, a fixture to start from).
// A test of the app or of a model goes through EngineClient (QbtEngine) instead.

#include "core/engines/qbt/QbtClient.hpp"
