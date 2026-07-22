function helper(): string {
  return "value";
}

type UserId = string;

export function main(): string {
  const userId: UserId = helper();
  return userId;
}
