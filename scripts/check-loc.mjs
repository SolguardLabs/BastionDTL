import { readFileSync, readdirSync, statSync } from "node:fs";
import { extname, join, relative, resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const ignored = new Set([".git", "build", "coverage", "node_modules", "out"]);
const sourceExtensions = new Set([".cpp", ".hpp", ".mjs", ".ts"]);
const totals = { cpp: 0, typescript: 0, support: 0, all: 0 };

function visit(directory) {
  for (const entry of readdirSync(directory)) {
    if (ignored.has(entry)) continue;
    const path = join(directory, entry);
    const relativePath = relative(root, path).replaceAll("\\", "/");
    if (relativePath.startsWith("tests/private/")) continue;
    const stat = statSync(path);
    if (stat.isDirectory()) {
      visit(path);
      continue;
    }
    const extension = extname(entry);
    if (!sourceExtensions.has(extension)) continue;
    const count = readFileSync(path, "utf8")
      .split(/\r?\n/)
      .filter((line) => line.trim()).length;
    totals.all += count;
    if (extension === ".cpp" || extension === ".hpp") totals.cpp += count;
    else if (extension === ".ts") totals.typescript += count;
    else totals.support += count;
  }
}

visit(root);
console.log(JSON.stringify(totals));
