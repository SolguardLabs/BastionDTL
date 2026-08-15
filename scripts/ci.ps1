$ErrorActionPreference = "Stop"

bun install --frozen-lockfile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

bun run ci
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
