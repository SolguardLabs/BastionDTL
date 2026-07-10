#!/usr/bin/env bash
set -euo pipefail

node scripts/build.mjs --warnings
bun test --timeout 30000 ./tests/node

