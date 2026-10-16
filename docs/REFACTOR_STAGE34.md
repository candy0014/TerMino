# Stage 34: Runtime State and Loop Cleanup

`PlayerPresentationState::blockRow/blockCol/blockRotation` became one `std::optional<Piece::Pose> activePose`, preventing partially updated remote-piece state.

The unused `GameLoopOutcome::flagDead` was deleted and `run_piece_loop()` now returns `GameLoopExit` directly. HOLD continuation and board queue initialization are bools rather than integer flags. The application facade names are now `run_frame`, `run_piece`, `reset_input_state`, `Game::run` and `Init::initialize_game`.
