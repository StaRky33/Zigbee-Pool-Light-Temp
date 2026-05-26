/*
 * version.h — Firmware version, single source of truth
 *
 * ── HOW TO INCREMENT ────────────────────────────────────────
 *  PATCH : bug fix, no behaviour change          1.0.0 → 1.0.1
 *  MINOR : new feature, backward compatible      1.0.x → 1.1.0
 *  MAJOR : breaking change / hardware revision   1.x.x → 2.0.0
 *
 * ── RELEASE CHECKLIST ───────────────────────────────────────
 *  1. Update the three numbers below
 *  2. Update FW_VERSION_STR to match
 *  3. Update version in poolLightTemp.mjs
 *  4. Update "version" in docs/manifest.json
 *  5. Run : idf.py merge-bin -o firmware/merged-flash.bin
 *  6. Commit : git commit -m "chore: bump version to x.y.z"
 *  7. Tag    : git tag vX.Y.Z && git push origin vX.Y.Z
 */

#pragma once

#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0

/* Human-readable string — must match the three numbers above */
#define FW_VERSION_STR      "1.0.0"
#define FW_VERSION_ZCL_STR  "\x05" "1.0.0"

/* ZCL ApplicationVersion (0x0001) encodes as (MAJOR<<4)|MINOR */
#define FW_VERSION_ZCL    ((FW_VERSION_MAJOR << 4) | FW_VERSION_MINOR)
