#!/usr/bin/env bash
set -euo pipefail

node scripts/build.mjs --warnings
bun run typecheck
bun test --timeout 30000 ./tests/node ./sdk
bun run format:check
bun run verify:repo

