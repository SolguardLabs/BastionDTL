import { describe, expect, test } from "bun:test";

import {
  BastionClient,
  BastionClientError,
  evaluateLiquidity,
  hhiContribution,
  ratioBps,
  type AccountLiquidityInput,
  type BastionTransport,
} from "./BastionClient";

const positions: AccountLiquidityInput[] = [
  {
    account: "custody:atlas",
    asset: "usdc",
    available: 400_000n,
    reserved: 100_000n,
    locked: 50_000n,
    expectedInflow: 80_000n,
    committedOutflow: 600_000n,
    inflowRecoveryBps: 8_000,
    outflowStressBps: 2_000,
    reserveReleaseBps: 5_000,
  },
  {
    account: "custody:forge",
    asset: "usdc",
    available: 500_000n,
    reserved: 80_000n,
    locked: 20_000n,
    expectedInflow: 100_000n,
    committedOutflow: 300_000n,
    inflowRecoveryBps: 7_000,
    outflowStressBps: 1_000,
    reserveReleaseBps: 5_000,
  },
];

const limits = {
  minCoverageBps: 11_000,
  maxSingleRequirementShareBps: 7_000,
  maxRequirementHhiBps: 6_000,
  maxTotalShortfall: 0n,
};

describe("Bastion liquidity SDK", () => {
  test("matches native portfolio calculations exactly", () => {
    const report = evaluateLiquidity(positions, limits);
    expect(report.totalUsableLiquidity).toBe(1_124_000n);
    expect(report.totalStressedOutflow).toBe(1_050_000n);
    expect(report.totalShortfall).toBe(206_000n);
    expect(report.totalSurplus).toBe(280_000n);
    expect(report.coverageBps).toBe(10_704);
    expect(report.requirementHhiBps).toBe(5_688);
    expect(report.withinLimits).toBe(false);
  });

  test("normalizes account order before concentration calculations", () => {
    const report = evaluateLiquidity([...positions].reverse(), limits);
    expect(report.accounts.map((item) => item.account)).toEqual(["custody:atlas", "custody:forge"]);
    expect(report.largestRequirementShareBps).toBe(6_857);
  });

  test("preserves integer floor semantics", () => {
    expect(ratioBps(720_000n, 1_050_000n)).toBe(6_857);
    expect(hhiContribution(6_857)).toBe(4_701);
  });

  test("rejects duplicate accounts", () => {
    expect(() => evaluateLiquidity([positions[0], positions[0]], limits)).toThrow(
      "Duplicate account",
    );
  });

  test("rejects unsafe numeric amounts", () => {
    expect(() =>
      evaluateLiquidity([{ ...positions[0], available: Number.MAX_VALUE }], limits),
    ).toThrow(BastionClientError);
  });
});

class StubTransport implements BastionTransport {
  readonly calls: string[][] = [];

  execute(args: readonly string[]): string {
    this.calls.push([...args]);
    if (args[0] === "list") {
      return JSON.stringify({ scenarios: ["snapshot", "liquidity", "governance"] });
    }
    return JSON.stringify({
      ok: true,
      scenario: args[1],
      networkId: "bastion-mainnet",
      epoch: 3,
      stateDigest: "0123456789abcdef0123456789abcdef",
      totalSupply: 1_500_000,
      checks: [{ name: "control", ok: true, detail: "ready" }],
      notes: [],
    });
  }
}

describe("Bastion native client", () => {
  test("lists and runs typed scenarios", () => {
    const transport = new StubTransport();
    const client = new BastionClient(transport);
    expect(client.listScenarios()).toEqual(["snapshot", "liquidity", "governance"]);
    expect(client.requireHealthy("snapshot").networkId).toBe("bastion-mainnet");
    expect(transport.calls).toEqual([
      ["list", "--json"],
      ["scenario", "snapshot", "--json"],
    ]);
  });

  test("rejects malformed scenario identifiers", () => {
    const client = new BastionClient(new StubTransport());
    expect(() => client.runScenario("../snapshot")).toThrow("Scenario name is invalid");
  });

  test("fails closed on malformed transport JSON", () => {
    const client = new BastionClient({ execute: () => "not-json" });
    expect(() => client.listScenarios()).toThrow("Invalid JSON");
  });

  test("surfaces rejected runtime controls", () => {
    const client = new BastionClient({
      execute: () =>
        JSON.stringify({
          ok: false,
          scenario: "liquidity",
          networkId: "bastion-mainnet",
          epoch: 1,
          stateDigest: "0123456789abcdef0123456789abcdef",
          totalSupply: 1_500_000,
          checks: [{ name: "coverage", ok: false, detail: "10704" }],
          notes: [],
        }),
    });
    expect(() => client.requireHealthy("liquidity")).toThrow("liquidity: coverage");
  });
});
