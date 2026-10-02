#pragma once

// CertificatePin — trust on first use for a server's TLS certificate (issue #2, Q27).
//
// Most qBittorrent Web UIs that speak HTTPS do it with a self-signed certificate, which the
// system refuses. Rather than turning certificate checks off, omaseed shows the certificate's
// SHA-256 fingerprint once and, when the person trusts it, saves that fingerprint with the
// server (servers.json; it is not a secret). From then on `QbtClient` accepts exactly that
// certificate for the server and still refuses any other one it would refuse without a pin.
// A certificate the system already trusts never needs a pin.
//
// A fingerprint is kept as 64 lowercase hex digits, no separators; `displayText()` is the
// form people read and compare ("AB:CD:…").

#include <QString>

class QSslCertificate;

namespace omaseed::core::CertificatePin {

/// SHA-256 over the certificate's DER bytes, as 64 lowercase hex digits; empty for a null
/// certificate.
QString fingerprint(const QSslCertificate &certificate);

/// `text` as a stored fingerprint: colons, spaces and case dropped. Empty when what is left
/// is not exactly 64 hex digits.
QString normalized(const QString &text);

/// "AB:CD:EF:…", the colon-separated upper-case pairs certificate tools print. Empty in,
/// empty out; anything that is not a fingerprint comes back as given.
QString displayText(const QString &fingerprint);

} // namespace omaseed::core::CertificatePin
