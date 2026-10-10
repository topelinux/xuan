/*
 * Minimal repro for sparse-array push/shift fast-path bug.
 *
 * Expected behavior (correct runtime):
 *   - no crash
 *   - shift on sparse array returns undefined
 *
 * Buggy runtime (before fix) may segfault when shift reads uninitialized slot.
 */

function onceCheck() {
  const arr = new Array(64); // sparse: length=64, no elements
  arr.push("x");             // index 64
  const v = arr.shift();     // should be undefined

  if (v !== undefined) {
    throw new Error("Unexpected shift result: " + String(v));
  }
}

function stress(rounds) {
  for (let i = 0; i < rounds; i++) {
    onceCheck();
    if ((i & 0x3fff) === 0) {
      console.log("ok", i);
    }
  }
}

const rounds = 500000;
console.log("start", rounds);
stress(rounds);
console.log("PASS");
