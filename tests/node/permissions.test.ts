import { expect, test } from "bun:test";
import { account, check, runScenario } from "../helpers/runner";

test("operator permission checks reject mismatched signer", () => {
  const state = runScenario("permissions");
  const rejected = state.settlements[0];
  expect(rejected.decision).toBe("rejected");
  expect(rejected.message).toContain("operator");
  expect(check(state, "unauthorized operator rejected").ok).toBe(true);
});

test("withdrawal controls distinguish operator, treasurer and frozen account", () => {
  const state = runScenario("permissions");
  expect(check(state, "operator withdrawal rejected").ok).toBe(true);
  expect(check(state, "treasurer withdrawal accepted").ok).toBe(true);
  expect(check(state, "frozen account blocks withdrawal").ok).toBe(true);

  const [operatorWithdrawal, treasurerWithdrawal, frozenWithdrawal] = state.withdrawals;
  expect(operatorWithdrawal.ok).toBe(false);
  expect(treasurerWithdrawal.ok).toBe(true);
  expect(frozenWithdrawal.ok).toBe(false);
  expect(account(state, "custody:atlas").available).toBe(999_000);
});
