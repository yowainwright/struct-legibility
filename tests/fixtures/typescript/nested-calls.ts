export function outer(): void {
  function inner(): void {
    helper();
  }

  inner();
}

function helper(): void {}
