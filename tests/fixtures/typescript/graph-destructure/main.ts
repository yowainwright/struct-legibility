import { helper } from "./helper";

export function shadowMain({ helper }: { helper: () => number }): number {
  return helper();
}
