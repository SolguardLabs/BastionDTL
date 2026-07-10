import { expect, test } from "bun:test";
import { account, check, runScenario } from "../helpers/runner";

test("operator rotation keeps handoff receipts processable", () => {
  const state = runScenario("rotation");
  expect(state.ok).toBe(true);
  expect(check(state, "pre-rotation receipt accepted").ok).toBe(true);
  expect(check(state, "post-rotation receipt accepted").ok).toBe(true);
  expect(check(state, "two receipts settled").ok).toBe(true);

  const [handoff, current] = state.settlements;
  expect(handoff.decision).toBe("accepted");
  expect(handoff.issuingOperator).toBe("operator:north");
  expect(handoff.appliedOperator).toBe("operator:south");
  expect(current.issuingOperator).toBe("operator:south");
  expect(current.appliedOperator).toBe("operator:south");
});

test("rotation updates account grant view and reconciliation counts", () => {
  const state = runScenario("rotation");
  const custody = account(state, "custody:atlas");
  expect(custody.currentOperator).toBe("operator:south");
  expect(custody.grantCount).toBe(2);
  expect(state.reconciliation.currentPolicies).toBe(2);
  expect(state.reconciliation.retiredPolicies).toBe(1);
  expect(state.notes.some((note) => note.startsWith("handoff:"))).toBe(true);
});
