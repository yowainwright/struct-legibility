import { helper } from "./helper";

export function shadowMain(helper: () => number): number {
  return helper();
}
