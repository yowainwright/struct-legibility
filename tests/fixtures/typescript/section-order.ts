type User = { id: string };
import { readFile } from "./filesystem";

export const loadUser = (path: string): User => ({ id: readFile(path) });
