import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { extname, join, relative, resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const expectedDocs = [
  "architecture.md",
  "change-control.md",
  "liquidity-risk.md",
  "operations.md",
  "runbooks.md",
  "sdk.md",
  "settlement-model.md",
];
const ignoredDirectories = new Set([".git", "build", "coverage", "node_modules", "out", "private"]);
const scannedExtensions = new Set([
  ".cpp",
  ".hpp",
  ".json",
  ".md",
  ".mjs",
  ".ps1",
  ".sh",
  ".ts",
  ".txt",
  ".yaml",
  ".yml",
]);
const forbiddenNarrative =
  /\b(?:ctf|vulnerabilit(?:y|ies)|vulnerable|exploits?|bugs?|laborator(?:y|ies)|laboratorios?|vulnerabilidades?)\b/i;
const placeholder = /\b(?:FIXME|TBD|XXX)\b/;
const failures = [];

function requireFile(path) {
  if (!existsSync(join(root, path))) failures.push(`Missing required file: ${path}`);
}

for (const file of [
  "README.md",
  "SECURITY.md",
  "LICENSE",
  "assets/banner.png",
  "package.json",
  "bun.lock",
  "CMakeLists.txt",
  "src/risk/liquidity.cpp",
  "src/governance/change_control.cpp",
  "sdk/BastionClient.ts",
  ".github/workflows/ci.yml",
  ".github/workflows/release-integrity.yml",
]) {
  requireFile(file);
}

const actualDocs = existsSync(join(root, "docs"))
  ? readdirSync(join(root, "docs"))
      .filter((file) => statSync(join(root, "docs", file)).isFile())
      .sort()
  : [];
if (JSON.stringify(actualDocs) !== JSON.stringify(expectedDocs)) {
  failures.push(`docs/ must contain exactly: ${expectedDocs.join(", ")}`);
}

const readme = readFileSync(join(root, "README.md"), "utf8");
if (!readme.includes("![Banner de BastionDTL](./assets/banner.png)")) {
  failures.push("README does not reference the canonical banner.");
}
if ((readme.match(/```mermaid/g) ?? []).length < 3) {
  failures.push("README must contain at least three Mermaid diagrams.");
}

const packageJson = JSON.parse(readFileSync(join(root, "package.json"), "utf8"));
if (packageJson.version !== "1.0.0") failures.push("package.json version must be 1.0.0.");
const cmake = readFileSync(join(root, "CMakeLists.txt"), "utf8");
const versionSource = readFileSync(join(root, "src/runtime/version.cpp"), "utf8");
if (!cmake.includes("VERSION 1.0.0")) failures.push("CMake version must be 1.0.0.");
if (!versionSource.includes('info.version = "1.0.0"')) {
  failures.push("BuildInfo version must be 1.0.0.");
}

function scan(directory) {
  for (const entry of readdirSync(directory)) {
    if (ignoredDirectories.has(entry)) continue;
    const path = join(directory, entry);
    const stat = statSync(path);
    if (stat.isDirectory()) {
      scan(path);
      continue;
    }
    if (!scannedExtensions.has(extname(entry))) continue;
    const relativePath = relative(root, path).replaceAll("\\", "/");
    if (relativePath === "scripts/verify-repository.mjs") continue;
    const content = readFileSync(path, "utf8");
    if (forbiddenNarrative.test(content))
      failures.push(`Forbidden public narrative: ${relativePath}`);
    if (placeholder.test(content)) failures.push(`Unresolved placeholder: ${relativePath}`);
  }
}

scan(root);

if (failures.length > 0) {
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(`Repository contract verified: ${actualDocs.length} docs, version 1.0.0.`);
