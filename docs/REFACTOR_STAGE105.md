# Stage 105: BOT translation units, naming and selection

> Current note: Stage 106 subsequently removes the `map`, Board façade and PPP catalog compatibility layers described below; this document records the Stage 105 state.

Stage 105 is the first deliberate BOT-organization refactor after the typed non-BOT core was completed. The goal is structural: remove obsolete strategies, compile each surviving strategy as an ordinary `.cpp` translation unit, make dispatch/name selection explicit, and keep the retained search/scoring algorithms behaviorally unchanged.

## Active BOT set

Runtime uses one contiguous internal ID space:

| ID | Name | Implementation source |
| ---: | --- | --- |
| 0 | `Disabled` | no BOT |
| 1 | `bot_while1` | old `bot_while2` logic |
| 2 | `bot_candy0014` | retained logic |
| 3 | `bot_mech` | old `bot_mech_all` logic |

The obsolete implementations `bot_while1`, `bot_while_1`, `bot_while2-`, and `bot_mech` were removed. The names `bot_while1` and `bot_mech` are now intentionally reused for the retained replacement implementations listed above.

## Header / implementation split

`include/tetr/bot/` contains declaration-only public headers:

```text
bot_while1.h
bot_candy0014.h
bot_mech.h
```

The implementation bodies live in:

```text
src/bot/bot_while1.cpp
src/bot/bot_candy0014.cpp
src/bot/bot_mech.cpp
```

Shared globals that emulate the old BOT environment are declared privately in `src/bot/bot_compat_environment.h` and defined/synchronized by `src/legacy/bot.cpp`. The legacy adapter dispatches through one implementation table rather than separate numbered branches.

## Static-initialization fix exposed by the split

The old `bot_while2` header relied on being textually included into the same translation unit as the legacy BOT environment. Its large global search arrays construct internal `board` values before `main()`, and that constructor read `bdp->QP_last_op`.

After moving the strategy into its own translation unit, cross-TU dynamic initialization order made that dependency unsafe. The renamed `bot_while1` now treats an unbound `bdp` as the historical default `QP_last_op = 8191`; `getpos()` still binds the current Board before live search. ASan and the legacy BOT boundary regression cover this path.

## Settings and numbering

Settings now exposes `AUTOPLAY -> Bot` as a choice list containing the names above. The player does not need to type an integer.

Current `user_config.txt` still stores `Autoplay` as an integer, using the new contiguous 0..3 IDs. Historical manually-edited values migrate as follows:

```text
old -1 (bot_while2)   -> new 1 (bot_while1)
old  2 (Candy0014)    -> new 2
old  4 (bot_mech_all) -> new 3 (bot_mech)
old  1 / 3            -> retired implementations; no exact strategy remains
```

Replay is intentionally different: the Replay compatibility codec continues to decode historical IDs and encodes the surviving strategies using their historical Replay values. This preserves old Replay text while Runtime and Settings use the new contiguous IDs.

## Regression guards

Stage 105 adds/extends tests that verify:

- exact internal BOT IDs and names;
- Settings uses a named choice instead of integer entry;
- historical Replay ID mapping remains explicit;
- all four surviving BOTs initialize through the centralized dispatch table;
- retired filenames cannot return;
- public BOT headers stay declaration-only;
- the implementations exist as `.cpp` files.

Validation performed for this stage:

- Debug: 36/36 tests passed;
- Release: 36/36 tests passed;
- 111/111 public headers compile standalone as the first include;
- all three active BOT modes were launched through the real Release `tetr` executable in a pseudo-TTY and reached live BOT execution without stderr or crashes;
- SIGINT terminal restoration remained correct;
- transformed BOT bodies were compared against their source implementations after accounting for the requested namespace/file rename and the static-initialization safety fix.
