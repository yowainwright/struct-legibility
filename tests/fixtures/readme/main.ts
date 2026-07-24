interface User {
  readonly name: string;
}

const greetingPrefix = "Hello";

export function main(user: User): string {
  return formatGreeting(user);
}

function formatGreeting(user: User): string {
  return `${greetingPrefix}, ${user.name}`;
}
