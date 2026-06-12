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
 *  5. Run : $idf_ver = (Get-Content .\main\version.h | Select-String 'FW_VERSION_STR\s+"([^"]+)"').Matches.Groups[1].Value; Write-Host "Extracted version: '$idf_ver'"; idf.py build; New-Item -ItemType Directory -Force -Path firmware; $output_path = Join-Path -Path (Resolve-Path firmware) -ChildPath "pool_light_temp_v${idf_ver}_merged.bin"; idf.py merge-bin -o $output_path
 *  6. Commit : git commit -m "chore: bump version to x.y.z"
 *  7. Tag    : git tag vX.Y.Z && git push origin vX.Y.Z
 */

#pragma once

#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  2

/* Human-readable string — must match the three numbers above */
#define FW_VERSION_STR      "1.0.2"
#define FW_VERSION_ZCL_STR  "\x05" "1.0.2"

/* ZCL ApplicationVersion (0x0001) encodes as (MAJOR<<4)|MINOR */
#define FW_VERSION_ZCL    ((FW_VERSION_MAJOR << 4) | FW_VERSION_MINOR)
