import { expect, test } from "bun:test";
import { account, check, runScenario } from "../helpers/runner";

test("merchant cycle reconciles source, beneficiary and reserve legs", () => {
  const state = runScenario("reserves");
  expect(state.ok).toBe(true);
  expect(check(state, "merchant cycle settled").ok).toBe(true);
  expect(check(state, "intraday capacity recorded").ok).toBe(true);
  expect(check(state, "internal supply conserved").ok).toBe(true);
  expect(check(state, "merchant payout completed").ok).toBe(true);
  expect(state.settlements).toHaveLength(9);

  const gross = state.settlements.reduce((total, settlement) => total + settlement.gross, 0);
  const fees = state.settlements.reduce((total, settlement) => total + settlement.operatorFee, 0);
  const reserves = state.settlements.reduce(
    (total, settlement) => total + settlement.reserveAmount,
    0,
  );
  const net = state.settlements.reduce(
    (total, settlement) => total + settlement.beneficiaryAmount,
    0,
  );

  expect(gross).toBe(810_000);
  expect(gross).toBe(fees + reserves + net);
  expect(account(state, "custody:atlas").available).toBe(1_000_000 - gross);
  expect(account(state, "beneficiary:merchant").available).toBe(0);
  expect(state.withdrawals[0].amount).toBe(net);
  expect(state.ledger.totalSupply).toBe(1_500_000 - net);
  expect(account(state, "fees:north").available).toBe(fees);
  expect(account(state, "reserve:atlas").available).toBe(reserves);
  expect(state.reconciliation.ok).toBe(true);
});
