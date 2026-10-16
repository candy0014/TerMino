# Refactor Stage 42: UDP Socket Layer

Moved the UDP primitive to `network/udp_socket.*`, renamed `Socket::UDP` to `Network::UdpSocket`, and moved address/exception names into the Network vocabulary. RuntimeServices and the relay server now share the same network-layer primitive.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
