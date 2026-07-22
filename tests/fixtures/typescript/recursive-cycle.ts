function isEven(value: number): boolean {
  return value === 0 || isOdd(value - 1);
}

export function isOdd(value: number): boolean {
  return value !== 0 && isEven(value - 1);
}
