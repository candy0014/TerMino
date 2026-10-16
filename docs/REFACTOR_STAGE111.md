# Stage 111: Blocking and Timed Waits

Long-lived hot polling loops now yield or wait for an event/deadline instead of spinning continuously.

- `UdpSocket` supports timed receive in addition to blocking receive.
- Lobby seed/setup/signal waits use blocking receive; the cancellable lobby UI uses a 10 ms bounded wait so Settings remains responsive.
- Multiplayer countdown sleeps to its deadline instead of executing a one-second empty spin.
- Replay placement/playback waits, low-PPS autoplay animation, live-piece polling, Settings choice/menu loops and post-game input polling yield while idle.
- Line-clear presentation accepts an optional idle-wait callback so production playback yields without slowing deterministic unit tests.

Wire formats and timing thresholds are unchanged.
