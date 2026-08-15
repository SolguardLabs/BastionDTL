import { describe, expect, test } from "bun:test";

import { runScenario } from "../helpers/runner";

describe("portfolio liquidity controls", () => {
  test("calculates stressed coverage and account shortfalls", () => {
    const state = runScenario("liquidity");
    const risk = state.liquidity;

    expect(state.ok).toBe(true);
    expect(risk).not.toBeNull();
    expect(risk?.totalStressedOutflow).toBe(1_050_000);
    expect(risk?.totalRecoveredInflow).toBe(134_000);
    expect(risk?.coverageBps).toBe(10_704);
    expect(risk?.totalShortfall).toBe(206_000);
    expect(risk?.withinLimits).toBe(false);
  });

  test("keeps concentration metrics and canonical account order", () => {
    const risk = runScenario("risk").liquidity;

    expect(risk?.accounts.map((item) => item.account)).toEqual(["custody:atlas", "custody:forge"]);
    expect(risk?.accounts[0].requirementShareBps).toBe(6_857);
    expect(risk?.accounts[0].hhiContributionBps).toBe(4_701);
    expect(risk?.requirementHhiBps).toBe(5_688);
    expect(risk?.largestRequirementShareBps).toBe(6_857);
    expect(risk?.digest).toHaveLength(20);
  });
});
