import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

export const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
export const binary = join(
  root,
  "out",
  process.platform === "win32" ? "bastiondtl.exe" : "bastiondtl",
);

export type ScenarioState = {
  ok: boolean;
  scenario: string;
  epoch: number;
  stateDigest: string;
  totalSupply: number;
  settlements: SettlementResult[];
  withdrawals: WithdrawalResult[];
  checks: ScenarioCheck[];
  notes: string[];
  liquidity: LiquidityReport | null;
  governance: ChangeControlReport | null;
  reconciliation: {
    ok: boolean;
    currentPolicies: number;
    retiredPolicies: number;
    findings: unknown[];
  };
  ledger: {
    accounts: AccountSnapshot[];
    settledReceipts: number;
    totalSupply: number;
  };
};

export type LiquidityReport = {
  totalImmediatelyAvailable: number;
  totalReleasableReserve: number;
  totalRecoveredInflow: number;
  totalStressedOutflow: number;
  totalSurplus: number;
  totalShortfall: number;
  coverageBps: number;
  requirementHhiBps: number;
  largestRequirementShareBps: number;
  withinLimits: boolean;
  digest: string;
  accounts: Array<{
    account: string;
    asset: string;
    immediatelyAvailable: number;
    releasableReserve: number;
    recoveredInflow: number;
    stressedOutflow: number;
    surplus: number;
    shortfall: number;
    requirementShareBps: number;
    hhiContributionBps: number;
  }>;
};

export type ChangeControlReport = {
  approvalQuorum: number;
  cancellationQuorum: number;
  digest: string;
  changes: Array<{
    id: string;
    target: string;
    action: string;
    state: string;
    predecessor: string;
    nonce: number;
    readyAt: number;
    expiresAt: number;
    approvalWeight: number;
    cancellationWeight: number;
    changeDigest: string;
  }>;
};

export type SettlementResult = {
  decision: "accepted" | "rejected";
  receiptId: string;
  sourceAccount: string;
  beneficiaryAccount: string;
  feeAccount: string;
  reserveAccount: string;
  issuingOperator: string;
  appliedOperator: string;
  appliedPolicy: string;
  gross: number;
  beneficiaryAmount: number;
  operatorFee: number;
  reserveAmount: number;
  message: string;
};

export type WithdrawalResult = {
  ok: boolean;
  account: string;
  actor: string;
  amount: number;
  message: string;
};

export type ScenarioCheck = {
  name: string;
  ok: boolean;
  detail: string;
};

export type AccountSnapshot = {
  id: string;
  asset: string;
  owner: string;
  status: string;
  available: number;
  reserved: number;
  locked: number;
  total: number;
  currentOperator: string;
  currentPolicy: string;
  grantCount: number;
};

export function ensureBuilt() {
  if (existsSync(binary)) {
    return;
  }
  const result = spawnSync(process.execPath, ["scripts/build.mjs"], {
    cwd: root,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    throw new Error(result.stderr || result.stdout || "build failed");
  }
}

export function runCli(args: string[]) {
  ensureBuilt();
  const result = spawnSync(binary, args, {
    cwd: root,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    throw new Error(
      `command failed: ${binary} ${args.join(" ")}\n${result.stderr}\n${result.stdout}`,
    );
  }
  return result.stdout;
}

export function runScenario(name: string): ScenarioState {
  return JSON.parse(runCli(["scenario", name, "--json"])) as ScenarioState;
}

export function account(state: ScenarioState, id: string): AccountSnapshot {
  const found = state.ledger.accounts.find((item) => item.id === id);
  if (!found) {
    throw new Error(`missing account ${id}`);
  }
  return found;
}

export function check(state: ScenarioState, name: string): ScenarioCheck {
  const found = state.checks.find((item) => item.name === name);
  if (!found) {
    throw new Error(`missing check ${name}`);
  }
  return found;
}
