/**
 * dshuf - Multi-key low-discrepancy shuffler
 * TypeScript / JavaScript implementation with optional WASM interface.
 * Zero dependencies, runs in Browser, Node.js, Deno, Bun, and Cloudflare Workers.
 */

export interface ShuffleOptions<T> {
  /** Single-key extractor function (e.g. t => t.artist) */
  key?: (item: T) => string | number;
  /** Multi-key extractor function returning array of key attributes (e.g. t => [t.artist, t.album]) */
  keys?: (item: T) => (string | number)[];
  /** Weights for multi-key hierarchy. Defaults to [1.0, 0.5, 0.25, ...] */
  weights?: number[];
  /** Jitter factor in [0.0, 1.0]. 0 = strictly balanced spacing, 1 = uniform random. Default 0.20 */
  jitter?: number;
  /** Sliding window size for distance evaluation. Default 256 */
  windowSize?: number;
  /** Starvation prevention factor beta. Default 0.50 */
  beta?: number;
  /** 64-bit integer seed for deterministic output */
  seed?: bigint | number;
}

/** 32-bit FNV-1a hash */
export function hashString(str: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i) & 0xff;
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

/** SplitMix64 PRNG */
export class Prng {
  private state: bigint;

  constructor(seed?: bigint | number) {
    if (seed !== undefined) {
      this.state = typeof seed === "bigint" ? seed : BigInt(seed);
    } else {
      this.state = BigInt(Math.floor(Math.random() * Number.MAX_SAFE_INTEGER));
    }
  }

  nextUint32(): number {
    this.state = (this.state + 0x9e3779b97f4a7c15n) & 0xffffffffffffffffn;
    let z = this.state;
    z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & 0xffffffffffffffffn;
    z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & 0xffffffffffffffffn;
    return Number((z ^ (z >> 31n)) & 0xffffffffn) >>> 0;
  }

  nextFloat(): number {
    return this.nextUint32() / 4294967296.0;
  }
}

interface WindowItem<T> {
  item: T;
  keys: number[];
  age: number;
}

/**
 * Streaming shuffler maintaining bounded sliding window memory.
 */
export class Stream<T> {
  private windowCap: number;
  private window: WindowItem<T>[] = [];
  private numKeys: number;
  private weights: number[];
  private jitter: number;
  private beta: number;
  private rng: Prng;

  private historyCap: number;
  private historyKeys: number[][] = [];

  constructor(options: ShuffleOptions<T> = {}) {
    this.windowCap = options.windowSize || 256;
    this.jitter = options.jitter ?? 0.2;
    this.beta = options.beta ?? 0.5;
    this.rng = new Prng(options.seed);

    this.weights = options.weights || [];
    this.numKeys = this.weights.length || 1;
    this.historyCap = this.windowCap;
  }

  push(item: T, keys?: (string | number)[]): void {
    const numericKeys: number[] = [];
    if (keys) {
      for (const k of keys) {
        numericKeys.push(typeof k === "number" ? k >>> 0 : hashString(String(k)));
      }
    }

    this.window.push({
      item,
      keys: numericKeys,
      age: 0,
    });
  }

  pop(): T | undefined {
    if (this.window.length === 0) return undefined;

    if (this.window.length === 1) {
      const it = this.window.shift()!;
      this.recordHistory(it.keys);
      return it.item;
    }

    let bestIdx = 0;
    let bestScore = Infinity;

    for (let i = 0; i < this.window.length; i++) {
      const it = this.window[i];
      let penalty = 0;

      for (let k = 0; k < it.keys.length; k++) {
        const kval = it.keys[k];
        const weight = this.weights[k] ?? Math.pow(0.5, k);

        let countK = 0;
        for (let j = 0; j < this.window.length; j++) {
          if (this.window[j].keys[k] === kval) countK++;
        }

        const idealSpacing = this.window.length / (countK > 0 ? countK : 1);
        const dist = this.findHistoryDist(k, kval);

        let dev = 0;
        if (dist > 0) {
          dev = (idealSpacing - dist) / idealSpacing;
          if (dev < -1.0) dev = -1.0;
        }
        penalty += weight * dev;
      }

      const ageBonus = this.beta * (it.age / this.windowCap);
      const noise = this.rng.nextFloat();
      const score = penalty - ageBonus + this.jitter * noise;

      if (score < bestScore) {
        bestScore = score;
        bestIdx = i;
      }
    }

    // Age remaining items
    for (let i = 0; i < this.window.length; i++) {
      if (i !== bestIdx) this.window[i].age++;
    }

    const chosen = this.window[bestIdx];
    this.recordHistory(chosen.keys);
    this.window.splice(bestIdx, 1);
    return chosen.item;
  }

  get count(): number {
    return this.window.length;
  }

  private recordHistory(keys: number[]): void {
    if (keys.length === 0) return;
    this.historyKeys.push([...keys]);
    if (this.historyKeys.length > this.historyCap) {
      this.historyKeys.shift();
    }
  }

  private findHistoryDist(keyLevel: number, keyVal: number): number {
    for (let d = 1; d <= this.historyKeys.length; d++) {
      const slot = this.historyKeys.length - d;
      if (this.historyKeys[slot][keyLevel] === keyVal) {
        return d;
      }
    }
    return 0;
  }
}

/**
 * Perform a balanced, low-discrepancy shuffle on an array of items.
 */
export function shuffle<T>(items: T[], options: ShuffleOptions<T> = {}): T[] {
  const n = items.length;
  if (n <= 1) return [...items];

  const rng = new Prng(options.seed);

  // Step 1: Initial Fisher-Yates
  const indices = Array.from({ length: n }, (_, i) => i);
  for (let i = n - 1; i > 0; i--) {
    const j = rng.nextUint32() % (i + 1);
    const tmp = indices[i];
    indices[i] = indices[j];
    indices[j] = tmp;
  }

  if (!options.key && !options.keys) {
    return indices.map(i => items[i]);
  }

  // Step 2: Extract keys
  const itemKeys: number[][] = items.map(it => {
    if (options.keys) {
      return options.keys(it).map(k => (typeof k === "number" ? k >>> 0 : hashString(String(k))));
    }
    if (options.key) {
      const k = options.key(it);
      return [typeof k === "number" ? k >>> 0 : hashString(String(k))];
    }
    return [];
  });

  const windowSize = options.windowSize || (n < 256 ? n : 256);
  const stream = new Stream<number>({
    windowSize,
    weights: options.weights,
    jitter: options.jitter,
    beta: options.beta,
    seed: rng.nextUint32(),
  });

  const result: number[] = [];
  let inPos = 0;

  // Prime window
  while (inPos < n && stream.count < windowSize) {
    const origIdx = indices[inPos++];
    stream.push(origIdx, itemKeys[origIdx]);
  }

  // Stream through window
  while (result.length < n) {
    const chosen = stream.pop();
    if (chosen === undefined) break;
    result.push(chosen);

    if (inPos < n) {
      const origIdx = indices[inPos++];
      stream.push(origIdx, itemKeys[origIdx]);
    }
  }

  return result.map(i => items[i]);
}