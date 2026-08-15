import { describe, expect, test } from "bun:test";

import { check, runScenario } from "../helpers/runner";

describe("signed change control", () => {
  test("executes only after delay and weighted quorum", () => {
    const state = runScenario("governance");
    const governance = state.governance;

    expect(state.ok).toBe(true);
    expect(check(state, "delay window enforced").ok).toBe(true);
    expect(governance?.approvalQuorum).toBe(65);
    expect(governance?.changes).toHaveLength(1);
    expect(governance?.changes[0].state).toBe("executed");
    expect(governance?.changes[0].approvalWeight).toBe(65);
  });

  test("produces deterministic governance evidence", () => {
    const first = runScenario("changes").governance;
    const second = runScenario("governance").governance;

    expect(first?.digest).toHaveLength(20);
    expect(first?.digest).toBe(second?.digest);
    expect(first?.changes[0].nonce).toBe(42);
    expect(first?.changes[0].readyAt).toBe(3);
    expect(first?.changes[0].expiresAt).toBe(8);
    expect(first?.changes[0].changeDigest).toHaveLength(20);
  });
});
