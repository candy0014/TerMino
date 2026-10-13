'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { loadReference } = require('./tetrio_engine_reference.cjs');

function verify(bundle, filename, options = {}) {
    const reference = loadReference(bundle);
    const data = options.data ?? JSON.parse(fs.readFileSync(filename, 'utf8'));
    if (data.users) {
        const imported=reference.importReplayFile(data);
        if (imported.users.length!==2 || imported.replay.rounds.length!==data.replay.rounds.length)
            throw Error('Official client file importer did not preserve the multiplayer container');
        for(const entry of [...data.replay.leaderboard,...data.replay.rounds.flat()]) {
            if(!data.users.some(user=>user.id===entry.id) || typeof entry.username!=='string')
                throw Error('Multiplayer identity mismatch');
            for(const name of ['apm','pps','vsscore'])
                if(!Number.isFinite(entry.stats[name])) throw Error(`Missing round-list statistic ${name}`);
        }
    }
    const wins = [0, 0];
    let total = 0;
    for (const [round, sides] of data.replay.rounds.entries()) {
        if (sides.length !== 2 || sides.filter(x => x.replay.results.gameoverreason === 'winner').length !== 1)
            throw Error(`Round ${round + 1} must have exactly two players and one winner`);
        const audit = JSON.parse(fs.readFileSync(path.join(path.dirname(filename), `round_${round + 1}.audit.json`), 'utf8'));
        for (const [side, entry] of sides.entries()) {
            const replay = new reference.Replay(entry.replay);
            const game = reference.create('replay', {replay});
            game.setHeadless(true);
            game.setGame(entry.replay.options);
            game.startGame();
            const snapshots = new Map(audit[side].map(x => [x.frame, x]));
            let checked = 0;
            let previousClears = {};
            for (let f = 0; f <= replay.frame; ++f) {
                game.doFrame();
                if (!snapshots.has(f)) continue;
                const expected = snapshots.get(f), s = game.state;
                const actual = {pieces:s.stats.piecesplaced, lines:s.stats.lines,
                    attack:s.stats.garbage.attack, sent:s.stats.garbage.sent, b2b:s.stats.btb,
                    combo:s.stats.combo, pending:s.impendingdamage.reduce((a,b)=>a+b.amt,0),
                    hold:s.hold, hole_rng:s.rngex.seed, hole_column:s.lastcolumn ?? -1, board:s.board};
                // The host snapshot is before consuming the next piece; v19
                // has already spawned it when the input frame finishes.
                if (expected.next) actual.next = [s.falling.type, ...s.bag].slice(0, expected.next.length);
                for (const key of Object.keys(actual)) {
                    if (JSON.stringify(actual[key]) !== JSON.stringify(expected[key])) {
                        let detail = `${JSON.stringify(actual[key])} != ${JSON.stringify(expected[key])}`;
                        if (key === 'board') {
                            const rows = [];
                            for (let r = 0; r < expected.board.length; ++r)
                                if (JSON.stringify(actual.board[r]) !== JSON.stringify(expected.board[r]))
                                    rows.push({row:r-100, actual:actual.board[r], expected:expected.board[r]});
                            detail = JSON.stringify(rows);
                        }
                        throw Error(`round ${round+1} side ${side+1} frame ${f} piece ${expected.pieces} ${key}: ${detail}`);
                    }
                }
                if ('spin' in expected) {
                    const counts = ['', 'singles', 'doubles', 'triples', 'quads', 'pentas'];
                    const name = expected.cleared_lines === 0
                        ? (expected.mini ? 'minitspins' : 'realtspins')
                        : (expected.spin ? (expected.mini ? 'minitspin' : 'tspin') : '') + counts[expected.cleared_lines];
                    if ((expected.spin || expected.cleared_lines) &&
                        s.stats.clears[name] - (previousClears[name] || 0) !== 1)
                        throw Error(`round ${round+1} side ${side+1} piece ${expected.pieces}: clear/spin mismatch (${name})`);
                }
                previousClears = structuredClone(s.stats.clears);
                ++checked;
            }
            if (checked !== audit[side].length) throw Error('Not every placement was checked');
            const won = entry.replay.results.gameoverreason === 'winner';
            if (won) ++wins[side];
            // Recorded end events are authoritative for the externally run match.
            if (game.state.stats.piecesplaced !== entry.replay.results.stats.piecesplaced)
                throw Error('End piece count mismatch');
            if (data.users) {
                const timeline = new reference.Replay(entry.replay).Preprocess('multi');
                if (timeline.frames !== replay.frame || !timeline.states.length ||
                    timeline.piece.some(frame => frame > replay.frame))
                    throw Error('Official replay timeline preprocessing failed');
                // Exercise actual seeking, including persistence of custom
                // native kick tables in the client's preprocessed snapshot.
                const seekGame=reference.create('replay', {replay:new reference.Replay(entry.replay)});
                seekGame.setHeadless(true);
                seekGame.setGame(entry.replay.options);
                seekGame.startGame();
                seekGame.injectState(timeline.states.at(-1));
                for(let f=seekGame.getFrame(); f<=replay.frame; ++f) seekGame.doFrame();
                for(const key of ['board','hold'])
                    if(JSON.stringify(seekGame.state[key])!==JSON.stringify(game.state[key]))
                        throw Error('Official replay seek mismatch: '+key);
                for(const key of ['piecesplaced','lines','btb','combo','garbage'])
                    if(JSON.stringify(seekGame.state.stats[key])!==JSON.stringify(game.state.stats[key]))
                        throw Error('Official replay seek statistics mismatch: '+key);
            }
            total += checked;
            if (options.onVerifiedSide) options.onVerifiedSide({round, side, entry, game});
            console.log(`round ${round+1} side ${side+1}: ${checked} placements verified`);
        }
        if (Math.max(...wins) >= 7 && round !== data.replay.rounds.length - 1)
            throw Error('Match continued after seven wins');
    }
    for (let side = 0; side < 2; ++side)
        if (data.replay.leaderboard[side].wins !== wins[side]) throw Error('Leaderboard score mismatch');
    if ((options.requireComplete || path.basename(filename) === 'match.ttrm') && Math.max(...wins) !== 7)
        throw Error('A final match must end at seven wins');
    console.log(`Verified ${total} placements; score ${wins[0]}:${wins[1]}; ${path.basename(filename)}`);
    return {placements:total, wins, data};
}
if (require.main === module) {
    if (process.argv.length !== 4) {
        console.error('Usage: node tools/verify_tetrio_match.cjs <official tetrio.js> <match.ttrm>');
        process.exit(2);
    }
    try { verify(process.argv[2], process.argv[3]); }
    catch (e) { console.error(e.stack.split('\n').filter(line => line.length < 1000).join('\n')); process.exitCode=1; }
}
module.exports = { verify };
