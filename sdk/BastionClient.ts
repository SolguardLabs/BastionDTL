import { spawnSync } from "node:child_process";

export const BPS_DENOMINATOR = 10_000n;

export type AmountInput = bigint | number | string;

export type AccountLiquidityInput = {
  account: string;
  asset: string;
  available: AmountInput;
  reserved: AmountInput;
  locked: AmountInput;
  expectedInflow: AmountInput;
  committedOutflow: AmountInput;
  inflowRecoveryBps: number;
  outflowStressBps: number;
  reserveReleaseBps: number;
};

export type AccountLiquidityResult = {
  account: string;
  asset: string;
  immediatelyAvailable: bigint;
  releasableReserve: bigint;
  recoveredInflow: bigint;
  stressedOutflow: bigint;
  usableLiquidity: bigint;
  surplus: bigint;
  shortfall: bigint;
  requirementShareBps: number;
  hhiContributionBps: number;
};

export type LiquidityLimits = {
  minCoverageBps: number;
  maxSingleRequirementShareBps: number;
  maxRequirementHhiBps: number;
  maxTotalShortfall: AmountInput;
};

export type PortfolioLiquidityResult = {
  accounts: AccountLiquidityResult[];
  totalImmediatelyAvailable: bigint;
  totalReleasableReserve: bigint;
  totalRecoveredInflow: bigint;
  totalStressedOutflow: bigint;
  totalUsableLiquidity: bigint;
  totalSurplus: bigint;
  totalShortfall: bigint;
  coverageBps: number;
  requirementHhiBps: number;
  largestRequirementShareBps: number;
  withinLimits: boolean;
};

export type RuntimeCheck = {
  name: string;
  ok: boolean;
  detail: string;
};

export type RuntimeScenario = {
  ok: boolean;
  scenario: string;
  networkId: string;
  epoch: number;
  stateDigest: string;
  totalSupply: number;
  checks: RuntimeCheck[];
  notes: string[];
  [key: string]: unknown;
};

export interface BastionTransport {
  execute(args: readonly string[]): string;
}

export type NativeTransportOptions = {
  binary: string;
  cwd?: string;
  timeoutMs?: number;
  environment?: NodeJS.ProcessEnv;
};

export class BastionClientError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = "BastionClientError";
    this.code = code;
  }
}

export class NativeBastionTransport implements BastionTransport {
  readonly #options: NativeTransportOptions;

  constructor(options: NativeTransportOptions) {
    if (!options.binary.trim()) {
      throw new BastionClientError("INVALID_BINARY", "A native binary path is required.");
    }
    this.#options = { ...options };
  }

  execute(args: readonly string[]): string {
    const result = spawnSync(this.#options.binary, [...args], {
      cwd: this.#options.cwd,
      encoding: "utf8",
      env: this.#options.environment,
      shell: false,
      timeout: this.#options.timeoutMs ?? 10_000,
    });
    if (result.error) {
      throw new BastionClientError("TRANSPORT_ERROR", result.error.message);
    }
    if (result.status !== 0) {
      const detail = (result.stderr || result.stdout || "native command failed").trim();
      throw new BastionClientError("COMMAND_REJECTED", detail);
    }
    return result.stdout;
  }
}

export class BastionClient {
  readonly #transport: BastionTransport;

  constructor(transport: BastionTransport) {
    this.#transport = transport;
  }

  listScenarios(): string[] {
    const response = parseJson<{ scenarios: unknown }>(
      this.#transport.execute(["list", "--json"]),
      "scenario list",
    );
    if (!Array.isArray(response.scenarios) || !response.scenarios.every(isNonEmptyString)) {
      throw new BastionClientError("INVALID_RESPONSE", "Scenario list has an invalid shape.");
    }
    return [...response.scenarios];
  }

  runScenario(name: string): RuntimeScenario {
    if (!isValidLabel(name)) {
      throw new BastionClientError("INVALID_SCENARIO", "Scenario name is invalid.");
    }
    const response = parseJson<Record<string, unknown>>(
      this.#transport.execute(["scenario", name, "--json"]),
      `scenario ${name}`,
    );
    validateScenario(response);
    return response as RuntimeScenario;
  }

  requireHealthy(name: string): RuntimeScenario {
    const scenario = this.runScenario(name);
    const failed = scenario.checks.filter((check) => !check.ok);
    if (!scenario.ok || failed.length > 0) {
      const labels = failed.map((check) => check.name).join(", ") || "scenario status";
      throw new BastionClientError("CONTROL_REJECTED", `${name}: ${labels}`);
    }
    return scenario;
  }

  assessLiquidity(
    positions: readonly AccountLiquidityInput[],
    limits: LiquidityLimits,
  ): PortfolioLiquidityResult {
    return evaluateLiquidity(positions, limits);
  }
}

export function evaluateLiquidity(
  positions: readonly AccountLiquidityInput[],
  limits: LiquidityLimits,
): PortfolioLiquidityResult {
  if (positions.length === 0) {
    throw new BastionClientError("EMPTY_PORTFOLIO", "At least one position is required.");
  }
  validateBps(limits.minCoverageBps, "minCoverageBps", 50_000);
  validateBps(limits.maxSingleRequirementShareBps, "maxSingleRequirementShareBps");
  validateBps(limits.maxRequirementHhiBps, "maxRequirementHhiBps");
  const maxShortfall = toAmount(limits.maxTotalShortfall, "maxTotalShortfall");
  const ordered = [...positions].sort((left, right) => left.account.localeCompare(right.account));
  const duplicate = ordered.find(
    (position, index) => index > 0 && position.account === ordered[index - 1].account,
  );
  if (duplicate) {
    throw new BastionClientError("DUPLICATE_ACCOUNT", `Duplicate account: ${duplicate.account}`);
  }

  const accounts = ordered.map(evaluateAccount);
  const totalImmediatelyAvailable = sum(accounts, "immediatelyAvailable");
  const totalReleasableReserve = sum(accounts, "releasableReserve");
  const totalRecoveredInflow = sum(accounts, "recoveredInflow");
  const totalStressedOutflow = sum(accounts, "stressedOutflow");
  const totalSurplus = sum(accounts, "surplus");
  const totalShortfall = sum(accounts, "shortfall");
  const totalUsableLiquidity =
    totalImmediatelyAvailable + totalReleasableReserve + totalRecoveredInflow;
  const coverageBps =
    totalStressedOutflow === 0n
      ? Number.MAX_SAFE_INTEGER
      : ratioBps(totalUsableLiquidity, totalStressedOutflow);

  for (const account of accounts) {
    account.requirementShareBps =
      totalStressedOutflow === 0n ? 0 : ratioBps(account.stressedOutflow, totalStressedOutflow);
    account.hhiContributionBps = hhiContribution(account.requirementShareBps);
  }
  const requirementHhiBps = accounts.reduce(
    (total, account) => total + account.hhiContributionBps,
    0,
  );
  const largestRequirementShareBps = Math.max(
    ...accounts.map((account) => account.requirementShareBps),
  );
  const withinLimits =
    totalShortfall <= maxShortfall &&
    coverageBps >= limits.minCoverageBps &&
    largestRequirementShareBps <= limits.maxSingleRequirementShareBps &&
    requirementHhiBps <= limits.maxRequirementHhiBps;

  return {
    accounts,
    totalImmediatelyAvailable,
    totalReleasableReserve,
    totalRecoveredInflow,
    totalStressedOutflow,
    totalUsableLiquidity,
    totalSurplus,
    totalShortfall,
    coverageBps,
    requirementHhiBps,
    largestRequirementShareBps,
    withinLimits,
  };
}

export function ratioBps(numerator: bigint, denominator: bigint): number {
  if (numerator < 0n || denominator <= 0n) {
    throw new BastionClientError("INVALID_RATIO", "Ratio operands are outside range.");
  }
  const ratio = (numerator * BPS_DENOMINATOR) / denominator;
  if (ratio > BigInt(Number.MAX_SAFE_INTEGER)) {
    return Number.MAX_SAFE_INTEGER;
  }
  return Number(ratio);
}

export function hhiContribution(shareBps: number): number {
  validateBps(shareBps, "shareBps");
  return Math.floor((shareBps * shareBps) / Number(BPS_DENOMINATOR));
}

function evaluateAccount(position: AccountLiquidityInput): AccountLiquidityResult {
  if (!isValidAccount(position.account) || !isValidLabel(position.asset)) {
    throw new BastionClientError("INVALID_POSITION", "Position identifiers are invalid.");
  }
  validateBps(position.inflowRecoveryBps, "inflowRecoveryBps");
  validateBps(position.outflowStressBps, "outflowStressBps", 40_000);
  validateBps(position.reserveReleaseBps, "reserveReleaseBps");
  const immediatelyAvailable = toAmount(position.available, "available");
  const reserved = toAmount(position.reserved, "reserved");
  toAmount(position.locked, "locked");
  const expectedInflow = toAmount(position.expectedInflow, "expectedInflow");
  const committedOutflow = toAmount(position.committedOutflow, "committedOutflow");
  const releasableReserve = applyBps(reserved, position.reserveReleaseBps);
  const recoveredInflow = applyBps(expectedInflow, position.inflowRecoveryBps);
  const stressedOutflow = committedOutflow + applyBps(committedOutflow, position.outflowStressBps);
  const usableLiquidity = immediatelyAvailable + releasableReserve + recoveredInflow;

  return {
    account: position.account,
    asset: position.asset,
    immediatelyAvailable,
    releasableReserve,
    recoveredInflow,
    stressedOutflow,
    usableLiquidity,
    surplus: maxBigInt(usableLiquidity - stressedOutflow, 0n),
    shortfall: maxBigInt(stressedOutflow - usableLiquidity, 0n),
    requirementShareBps: 0,
    hhiContributionBps: 0,
  };
}

function applyBps(amount: bigint, bps: number): bigint {
  return (amount * BigInt(bps)) / BPS_DENOMINATOR;
}

function toAmount(value: AmountInput, field: string): bigint {
  let amount: bigint;
  try {
    if (typeof value === "number" && !Number.isSafeInteger(value)) {
      throw new Error("unsafe integer");
    }
    amount = BigInt(value);
  } catch {
    throw new BastionClientError("INVALID_AMOUNT", `${field} is not an integer amount.`);
  }
  if (amount < 0n) {
    throw new BastionClientError("INVALID_AMOUNT", `${field} cannot be negative.`);
  }
  return amount;
}

function validateBps(value: number, field: string, maximum = 10_000): void {
  if (!Number.isInteger(value) || value < 0 || value > maximum) {
    throw new BastionClientError("INVALID_BPS", `${field} is outside range.`);
  }
}

function sum(
  accounts: readonly AccountLiquidityResult[],
  key: keyof Pick<
    AccountLiquidityResult,
    | "immediatelyAvailable"
    | "releasableReserve"
    | "recoveredInflow"
    | "stressedOutflow"
    | "surplus"
    | "shortfall"
  >,
): bigint {
  return accounts.reduce((total, account) => total + account[key], 0n);
}

function parseJson<T>(value: string, context: string): T {
  try {
    return JSON.parse(value) as T;
  } catch {
    throw new BastionClientError("INVALID_JSON", `Invalid JSON returned for ${context}.`);
  }
}

function validateScenario(value: Record<string, unknown>): asserts value is RuntimeScenario {
  const validChecks =
    Array.isArray(value.checks) &&
    value.checks.every(
      (check) =>
        typeof check === "object" &&
        check !== null &&
        isNonEmptyString((check as RuntimeCheck).name) &&
        typeof (check as RuntimeCheck).ok === "boolean" &&
        typeof (check as RuntimeCheck).detail === "string",
    );
  if (
    typeof value.ok !== "boolean" ||
    !isNonEmptyString(value.scenario) ||
    !isNonEmptyString(value.networkId) ||
    !Number.isSafeInteger(value.epoch) ||
    !isNonEmptyString(value.stateDigest) ||
    !validChecks ||
    !Array.isArray(value.notes)
  ) {
    throw new BastionClientError("INVALID_RESPONSE", "Scenario response has an invalid shape.");
  }
}

function isValidAccount(value: string): boolean {
  return /^[a-z][a-z0-9-]*:[a-z][a-z0-9-]*$/.test(value);
}

function isValidLabel(value: string): boolean {
  return /^[a-z][a-z0-9-]*$/.test(value);
}

function isNonEmptyString(value: unknown): value is string {
  return typeof value === "string" && value.length > 0;
}

function maxBigInt(left: bigint, right: bigint): bigint {
  return left > right ? left : right;
}
