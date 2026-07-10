import { expect, test } from "bun:test";
import { account, check, runScenario } from "../helpers/runner";

test("signed receipt settles beneficiary, fee and reserve legs", () => {
  const state = runScenario("receipts");
  expect(state.ok).toBe(true);
  expect(check(state, "receipt accepted").ok).toBe(true);
  expect(check(state, "supply conserved").ok).toBe(true);

  const settlement = state.settlements[0];
  expect(settlement.decision).toBe("accepted");
  expect(settlement.gross).toBe(25_000);
  expect(settlement.operatorFee).toBe(100);
  expect(settlement.reserveAmount).toBe(62);
  expect(settlement.beneficiaryAmount).toBe(24_838);

  expect(account(state, "beneficiary:merchant").available).toBe(24_838);
  expect(account(state, "fees:north").available).toBe(100);
  expect(account(state, "reserve:atlas").available).toBe(62);
  expect(account(state, "custody:atlas").available).toBe(975_000);
});

test("receipt scenario exposes reconciliation without findings", () => {
  const state = runScenario("receipts");
  expect(state.reconciliation.ok).toBe(true);
  expect(state.reconciliation.findings.length).toBe(0);
  expect(state.reconciliation.currentPolicies).toBe(2);
});
