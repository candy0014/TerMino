// Offline conformance oracle, NOT part of the game or a replacement game engine.
// The caller supplies an official client bundle; no proprietary code is vendored.
// Fail closed when the bundle changes: extraction/engine semantics need review.
'use strict';
const fs = require('node:fs');
const crypto = require('node:crypto');
const vm = require('node:vm');

const reviewedSha256 = 'b310aaf13ac0d7ee73c87c58abcdec87e1267217e182c10f4c9cb40dc5d73c92';

function loadReference(filename) {
    const bytes = fs.readFileSync(filename);
    const digest = crypto.createHash('sha256').update(bytes).digest('hex');
    if (digest !== reviewedSha256) throw Error('Unreviewed TETR.IO client SHA-256: ' + digest);
    const source = bytes.toString('utf8');
    const begin = source.indexOf('class C{constructor(e){this._seed=e%2147483647');
    const end = source.indexOf('function fe(e,t,n,s,i){return Ke(n-155,t)}ge.init()', begin);
    if (begin < 0 || end <= begin) throw Error('Cannot locate reviewed engine');
    // Supply only outer pure utility functions used by the headless engine.
    // Strict shallow equality is used to compare default option values.
    const strictShallowEqual = (a, b) => {
        if (Object.is(a, b)) return true;
        if (!a || !b || typeof a !== 'object' || typeof b !== 'object') return false;
        const keys = Object.keys(a);
        return keys.length === Object.keys(b).length &&
            keys.every(key => Object.hasOwn(b, key) && Object.is(a[key], b[key]));
    };
    let serverMode = true;
    const sandbox = {
        console, performance, setTimeout, clearTimeout,
        // Match the bundle's JSON clone (functions in constants are omitted).
        Vt: value => value === undefined ? value : JSON.parse(JSON.stringify(value)),
        qt: () => serverMode,
        // Preprocess sets options before enabling headless mode. Suppress its
        // presentation-only callbacks just as a server build does; core engine
        // methods and input processing remain the original bundle's code.
        Ut: () => {}, Xt: callback => callback(),
        b: value => value === null ? 'null' : Array.isArray(value) ? 'array' : typeof value,
        t: { strictShallowEqual },
    };
    vm.createContext(sandbox);
    // Execute the unchanged engine section; UI, networking, and login are absent.
    vm.runInContext(source.slice(begin, end) +
        ';ge.init();globalThis.reference={create:v,Replay:L,ReplayFile:B,constants:w};', sandbox,
        { timeout: 10000, filename: 'official-tetrio-engine-v19.js' });
    return {...sandbox.reference, importReplayFile(data) {
        // Exercise the same file upgrade/wrapper path used by client drag/drop,
        // without claiming this replaces a browser's visual UI test.
        serverMode=false;
        try { return new sandbox.reference.ReplayFile(
            sandbox.reference.ReplayFile.Update(structuredClone(data))); }
        finally { serverMode=true; }
    }};
}

module.exports = { loadReference, reviewedSha256 };
