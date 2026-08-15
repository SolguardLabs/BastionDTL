import { expect, test } from "bun:test";
import { runCli, runScenario } from "../helpers/runner";

test("help exposes scenario commands", () => {
  const stdout = runCli(["--help"]);
  expect(stdout).toContain("BastionDTL 1.0.0");
  expect(stdout).toContain("scenario <name>");
  expect(stdout).toContain("rotation");
});

test("list emits supported scenarios as json", () => {
  const stdout = runCli(["list", "--json"]);
  const parsed = JSON.parse(stdout) as { scenarios: string[] };
  expect(parsed.scenarios).toContain("receipts");
  expect(parsed.scenarios).toContain("permissions");
  expect(parsed.scenarios).toContain("rotation");
  expect(parsed.scenarios).toContain("closure");
});

test("snapshot scenario returns stable top-level contract", () => {
  const state = runScenario("snapshot");
  expect(state.ok).toBe(true);
  expect(state.scenario).toBe("snapshot");
  expect(state.stateDigest).toHaveLength(32);
  expect(state.totalSupply).toBe(1_500_000);
  expect(state.ledger.accounts.length).toBeGreaterThan(5);
  expect(state.reconciliation.ok).toBe(true);
});
