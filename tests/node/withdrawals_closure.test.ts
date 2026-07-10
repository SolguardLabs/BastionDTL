import { expect, test } from "bun:test";
import { account, check, runScenario } from "../helpers/runner";

test("withdrawal scenario applies owner-only policy on forge custody", () => {
  const state = runScenario("withdrawals");
  expect(state.ok).toBe(true);
  expect(check(state, "owner withdrawal accepted").ok).toBe(true);
  expect(check(state, "cross-owner withdrawal rejected").ok).toBe(true);
  expect(check(state, "remaining balance updated").ok).toBe(true);
  expect(account(state, "custody:forge").available).toBe(475_000);
});

test("closure scenario closes empty account and rejects funded closure", () => {
  const state = runScenario("closure");
  expect(state.ok).toBe(true);
  expect(check(state, "empty account closed").ok).toBe(true);
  expect(check(state, "funded account closure rejected").ok).toBe(true);
  expect(account(state, "custody:empty").status).toBe("closed");
  expect(account(state, "custody:atlas").status).toBe("open");
});
